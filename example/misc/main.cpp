#include <atomic>
#include <chrono>
#include <thread>
#include <AsyncFw/MainThread>
#include <AsyncFw/DataArrayTcpServer>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/DataArraySocket>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

int main(int, char **) {
  constexpr uint16_t kPort = 18998;
  constexpr int kIterations = 1000;

  Thread serverOwner("ServerOwner");
  Thread clientOwner("ClientOwner");
  serverOwner.start();
  clientOwner.start();

  DataArrayTcpServer *server = nullptr;
  serverOwner.invoke([&]() {
    server = new DataArrayTcpServer("DeadlockServer");
    // maxThreads = 2: Поток 1 занят anchor-сокетом, Поток 2 постоянно
    // создаётся и уничтожается (e == true -> lock pool->mutex)
    server->init(30000, 0, 10000, 2, 64);
    if (!server->listen("127.0.0.1", kPort)) { logEmergency() << "Failed to listen on port" << kPort; }
  }, true);

  DataArrayTcpClient *client = nullptr;
  clientOwner.invoke([&]() {
    client = new DataArrayTcpClient("DeadlockClient");
    client->init(30000, 0, 10000, 4, 64);
    client->setReconnectTimeout(0);
  }, true);

  std::atomic<bool> stop {false};
  std::atomic<int> completed {0};

  std::thread watchdog([&]() {
    int last = -1;
    while (!stop.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      if (stop.load(std::memory_order_relaxed)) break;
      int cur = completed.load(std::memory_order_relaxed);
      if (cur == last) { logEmergency() << "DEADLOCK DETECTED in findMinimalSocketsThread() at iteration" << cur; }
      last = cur;
    }
  });

  // Занимаем Поток 1 на сервере, чтобы threads_.size() не опускался ниже 1
  DataArraySocket *anchor = client->createSocket();
  std::atomic<bool> anchorActive {false};
  FunctionConnectionGuard anchorGuard;
  clientOwner.invoke([&]() {
    anchorGuard = client->connectionStateChanged.connect([&](const DataArraySocket *s) {
      if (s == anchor && s->state() == AbstractSocket::State::Active) anchorActive.store(true);
    });
  }, true);
  client->connectToHost(anchor, "127.0.0.1", kPort);
  while (!anchorActive.load()) std::this_thread::yield();

  // Параллельный поток создаёт гонку: пока один сокет закрывается на Потоке 2 (e == true),
  // второй пытается подключиться и заставляет incomingConnection() вызвать findMinimalSocketsThread()
  std::thread racer([&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      DataArraySocket *s = client->createSocket();
      if (!s) continue;
      client->connectToHost(s, "127.0.0.1", kPort);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      client->destroySocket(s);
    }
  });

  for (int i = 0; i < kIterations; ++i) {
    lsInfo() << "i" << i;
    DataArraySocket *s = client->createSocket();
    if (s) {
      client->connectToHost(s, "127.0.0.1", kPort);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      client->destroySocket(s);
    }
    completed.fetch_add(1, std::memory_order_relaxed);
  }


  lsNotice() << "A1.0";
  //server->close();
  lsNotice() << "A1.1";

  stop.store(true, std::memory_order_relaxed);
  lsNotice() << "A2";
  racer.join();
  lsNotice() << "A3";
  watchdog.join();
  lsNotice() << "A4";

  clientOwner.invoke([&]() { anchorGuard = {}; }, true);
  client->destroySocket(anchor);

  clientOwner.invoke([&]() { delete client; }, true);
  serverOwner.invoke([&]() { delete server; }, true);

  clientOwner.quit();
  clientOwner.waitFinished();
  serverOwner.quit();
  serverOwner.waitFinished();

  logNotice() << "Finished without deadlock, iterations:" << completed.load();
  return 0;
}
