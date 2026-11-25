#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <AsyncFw/MainThread>
#include <AsyncFw/DataArrayTcpServer>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/DataArraySocket>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

int main(int, char **) {
  constexpr uint16_t kPort = 18996;
  constexpr int kIterations = 10;

  // Отдельный поток-владелец для клиента со свободным циклом событий exec()
  Thread clientOwner("ClientOwner");
  clientOwner.start();

  DataArrayTcpServer server("DeadlockProbeServer");
  if (!server.listen("127.0.0.1", kPort)) {
    logError() << "Failed to listen on port" << kPort;
    return 1;
  }

  DataArrayTcpClient *client = nullptr;
  clientOwner.invoke([&]() {
    client = new DataArrayTcpClient("DeadlockProbeClient");
    client->setReconnectTimeout(0);
  }, true);

  std::atomic<bool> stop {false};
  std::atomic<int> completed {0};

  // Управляющий цикл в отдельном std::thread — не блокирует ни MainThread, ни clientOwner
  Thread driver;
  driver.invoke([&]() {
    for (int i = 0; i < kIterations && !stop.load(); ++i) {
      DataArraySocket *sock = client->createSocket();
      std::atomic<bool> active {false};

      FunctionConnectionGuard g = client->connectionStateChanged.connect([&](const DataArraySocket *s) {
        lsInfo() << (int)s->state() << i;
        if (s == sock && s->state() == AbstractSocket::State::Active) active.store(true);
      });

      client->connectToHost(sock, "127.0.0.1", kPort);

      auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
      while (!active.load() && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }

      g = {};
      client->destroySocket(sock);
      completed.fetch_add(1);
    }
    //   server.quit();
    driver.quit();
  });

  AbstractThread *mainThread = AbstractThread::current();

  mainThread->invoke([&]() { driver.start(); });

  driver.finished.connect([&]() {
    mainThread->invoke([&]() {
      server.close();
      mainThread->quit();
    }, true);
  });

  // Параллельный опрос server.sockets() из внешнего потока
  std::thread poller([&]() {
    while (!stop.load()) {
      volatile int n = server.sockets();
      (void)n;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  //  std::thread watchdog([&]() {
  //    auto start = std::chrono::steady_clock::now();
  //    while (!stop.load()) {
  //      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) logEmergency() << "DEADLOCK DETECTED in DataArrayAbstractTcp::sockets() at iteration" << completed.load();
  //      std::this_thread::sleep_for(std::chrono::milliseconds(50));
  //    }
  //  });

  logNotice() << "Start deadlock reproducer (C++23)...";
  int ret = MainThread::exec();

  stop.store(true);
   poller.join();
  // watchdog.join();

  clientOwner.invoke([&]() { delete client; }, true);
  clientOwner.quit();
  clientOwner.waitFinished();

  logNotice() << "Finished successfully, iterations:" << completed.load();
  return ret;
}
