# AsyncFw

Async Framework is c++ runtime with timers, poll notifiers, sockets, coroutines, etc. 

Is a software framework that allows developers to build applications using asynchronous programming, which runs operations concurrently without blocking the main thread.
See examples for details.

Timer example:
```c++
#include <iostream>
#include <chrono>
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Timer.h>

int main(int argc, char *argv[]) {
  int cnt = 0;
  AsyncFw::Timer timer1;
  timer1.start(1000);
  AsyncFw::Timer timer2;
  timer2.start(2000);

  timer1.timeout([&cnt]() {
    std::cout << std::chrono::system_clock::now() << " timer1 timeout" << std::endl;
    if (++cnt == 10) AsyncFw::MainThread::exit(0);
  });

  timer2.timeout([]() { std::cout << std::chrono::system_clock::now() << " timer2 timeout" << std::endl; });

  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
```
PollNotifier example (Unix only):
```c++
#include <iostream>
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/PollNotifier.h>

int main(int argc, char *argv[]) {
  AsyncFw::PollNotifier notifier(STDIN_FILENO);
  if (isatty(STDIN_FILENO)) {
    notifier.notify([&notifier](AsyncFw::AbstractThread::PollEvents e) {
      char buf[128];
      int r  = read(STDIN_FILENO, buf, sizeof(buf) - 1);
      buf[r] = 0;
      if (r == 2 && buf[0] == 'q') AsyncFw::MainThread::exit();
      std::cout << "stdin: " << buf;
    });
  } else {
    std::cout << "stdin is not connected to the terminal";
    return -1;
  }
  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
```
Log example:
```c++
#include <AsyncFw/Log.h>
#include <AsyncFw/MainThread.h>

int main(int argc, char *argv[]) {
  AsyncFw::Log(1000, "log-file-name");

  logTrace() << "Trace";
  logDebug() << "Debug";
  logInfo() << "Info";
  logNotice() << "Notice";
  logWarning() << "Warning";
  logError() << "Error";
  logAlert() << "logAlert";
  logEmergency() << "Emergency";  // terminate application

  return 0;
}
```
Executing method in another thread example:
```c++
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Log.h>

int main(int argc, char *argv[]) {
  AsyncFw::AbstractThread *_mainThread = AsyncFw::AbstractThread::currentThread();
  AsyncFw::Thread thread1("T1");
  AsyncFw::Thread thread2("T2");

  thread1.start();
  thread2.start();

  logDebug() << "Main id:" << _mainThread->id();
  logDebug() << "T1 id:" << thread1.id();
  logDebug() << "T2 id:" << thread2.id();

  thread1.invokeMethod([&thread2, _mainThread]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    logInfo() << "run in thread" << ct->name() << ct->id();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    thread2.invokeMethod([_mainThread]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << "run in thread" << ct->name() << ct->id();
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      _mainThread->invokeMethod([_mainThread]() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        logInfo() << "run in thread" << ct->name() << ct->id();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        logInfo() << "exit application";
        AsyncFw::MainThread::exit(0);
      });
    });
  });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  return ret;
}
```
FunctionConnector example:
```c++
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/core/FunctionConnector.h>
#include <AsyncFw/Timer.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Log.h>

class Sender {
public:
  Sender() {
    timer.timeout([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << cnt << "send from thread:" << ct->name() << ct->id();
      connector(cnt++);
      if (cnt == 3) AsyncFw::MainThread::exit(0);
    });
    timer.start(1000);
  }

  mutable AsyncFw::FunctionConnectorProtected<Sender>::Connector<int> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  Receiver(const std::string &name, const Sender &sender) {
    sender.connector([name_ = name](int val) {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << name_ << "received" << val << "run in thread" << ct->name() << ct->id();
    });
  }
};

int main(int argc, char *argv[]) {
  Sender *sender;

  AsyncFw::Thread thread("SenderThread");
  thread.start();
  thread.invokeMethod(
      [&sender]() {
        sender = new Sender;  // created in thread SenderThread
      },
      true);

  Receiver receiver1("R1", *sender);
  Receiver receiver2("R2", *sender);

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  delete sender;
  return ret;
}
```
ThreadPool example:
```c++
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/ThreadPool.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Log.h>

int main(int argc, char *argv[]) {
  AsyncFw::ThreadPool threadPool;

  AsyncFw::AbstractThread *_t = threadPool.createThread("SyncExample");

  AsyncFw::ThreadPool::sync(_t, []() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    logInfo() << "sync run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async([]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    logInfo() << "async run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async(
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        logInfo() << "async run in thread" << ct->name() << ct->id();
      },
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        logNotice() << "result without value, run in thread" << ct->name() << ct->id();
      });

  AsyncFw::ThreadPool::async(
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        logInfo() << "async run in thread" << ct->name() << ct->id();
        return 1;
      },
      [](int r) {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        logNotice() << "result:" << r << "run in thread" << ct->name() << ct->id();
        AsyncFw::MainThread::exit(0);
      });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton" << ret;
  return ret;
}
```
Coroutine example:
```c++
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/ThreadPool.h>
#include <AsyncFw/Coroutine.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Log.h>

AsyncFw::CoroutineTask task() {
  AsyncFw::CoroutineAwait await([](AsyncFw::CoroutineHandle h) {
    AsyncFw::ThreadPool::async(
        [h]() {
          AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
          logInfo() << "task: run in thread" << ct->name() << ct->id();
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        },
        [h]() {
          AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
          logInfo() << "task: resume in thread" << ct->name() << ct->id();
          h.resume();
        });
  });
  co_await await;
  AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
  logNotice() << "task: resumed in thread" << ct->name() << ct->id();
}

int main(int argc, char *argv[]) {
  AsyncFw::ThreadPool threadPool;

  task();

  auto coro_task([]() -> AsyncFw::CoroutineTask {
    AsyncFw::CoroutineAwait await([](AsyncFw::CoroutineHandle h) {
      AsyncFw::ThreadPool::async(
          [h]() {
            AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
            logInfo() << "coro_task: run in thread" << ct->name() << ct->id();
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
          },
          [h]() {
            AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
            logInfo() << "coro_task: resume in thread" << ct->name() << ct->id();
            h.resume();
          });
    });
    co_await await;
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    logNotice() << "coro_task: resumed in thread" << ct->name() << ct->id();
    AsyncFw::MainThread::exit(0);
  });

  coro_task();

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  return ret;
}
```
Tls socket example:
```c++
#include <core/Thread.h>
#include <MainThread.h>
#include <core/TlsSocket.h>
#include <core/TlsContext.h>
#include <AddressInfo.h>
#include <Log.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  void stateEvent() {
    logDebug() << "State event:" << static_cast<int>(state());
    if (state() == Active) {
      logDebug() << "Send request";
      write("GET /a-ucontrol/AsyncFw HTTP/1.1\r\nHost:github.com\r\nConnection:close\r\n\r\n");
    } else if (error() >= Closed) {
      if (error() != Closed) logError() << errorString();
      else {
        logNotice() << "Received:" << da;
        logDebug() << da.view(0, 512) << "...";
      }
      AsyncFw::MainThread::exit(0);
    }
  }
  void readEvent() {
    AsyncFw::DataArray _da = read();
    logTrace() << "Read event:" << _da.size() << std::endl << _da.view(0, 256);
    da += _da;
  }

private:
  AsyncFw::DataArray da;
};

int main(int argc, char *argv[]) {
  AsyncFw::TlsContext context;

  if (!context.setDefaultVerifyPaths()) {
    logError("Can't set default verify paths");
    return -1;
  }
  context.setVerifyName("github.com");
  TcpSocket socket;
  socket.setContext(context);

  AsyncFw::AddressInfo addressInfo;
  addressInfo.resolve("github.com");
  addressInfo.completed([&socket](int r, const std::vector<std::string> &list) {
    if (r == 0 && !list.empty()) {
      for (const std::string _s : list) logNotice() << _s;
      socket.connect(list[0], 443);
    }
  });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  return ret;
}
```
Container task example:
```c++
#include <queue>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Timer.h>
#include <AsyncFw/Task.h>
#include <AsyncFw/core/LogStream.h>

int main(int argc, char *argv[]) {
  AsyncFw::Thread thread;
  AsyncFw::Timer timer;
  std::queue<std::shared_ptr<AsyncFw::AbstractTask>> tasks;

  for (int i = 0; i != 10; ++i) {
    std::shared_ptr<AsyncFw::AbstractTask> task = std::shared_ptr<AsyncFw::AbstractTask>(new AsyncFw::Task([](std::any *data) { logInfo() << "task:" << std::any_cast<int>(*data); }, &thread));
    task->setData(i);
    tasks.push(task);
  }

  timer.timeout([&tasks]() {
    if (tasks.empty()) {
      AsyncFw::MainThread::exit(0);
      return;
    }
    lsInfoGreen() << tasks.front()->running();
    tasks.front()->invoke();
    lsInfoGreen() << tasks.front()->running();
    tasks.pop();
  });

  thread.start();
  timer.start(500);

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Applicaiton";
  return ret;
}
```
Listen socket example:
```c++
#include <AsyncFw/MainThread.h>
#include <AsyncFw/core/TlsSocket.h>
#include <AsyncFw/Log.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static TcpSocket *create() { return new TcpSocket; }
  void stateEvent() { logDebug() << "State event:" << static_cast<int>(state()); }
  void readEvent() { received(read()); }
  AsyncFw::FunctionConnectorProtected<TcpSocket>::Connector<const AsyncFw::DataArray &> received;
};

int main(int argc, char *argv[]) {
  AsyncFw::ListenSocket ls;
  ls.setIncomingConnection([](int fd, const std::string &address) {
    TcpSocket *socket = TcpSocket::create();
    socket->setDescriptor(fd);
    socket->received([socket](const AsyncFw::DataArray &data) {
      logNotice() << "received:" << data;
      socket->write("Answer\n");
      AsyncFw::MainThread::exit(0);
    });
    logInfo() << "Incoming:" << fd << address;
    return true;
  });

  ls.listen("0.0.0.0", 18080);

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Applicaiton";

  return ret;
}
```
SystemProcess example (Unix only):
```c++
#include <MainThread.h>
#include <linux/SystemProcess.h>
#include <Timer.h>
#include <core/LogStream.h>

int main(int argc, char *argv[]) {
  AsyncFw::SystemProcess process;
  process.output([](const std::string &str, bool err) {
    if (!err) logInfo() << "OUT:" << '\n' + str;
    else { logError() << "ERR:" << '\n' + str; }
  });
  process.stateChanged([](AsyncFw::SystemProcess::State _s) {
    if (_s == AsyncFw::SystemProcess::Running) return;
    AsyncFw::MainThread::exit(0);
  });

  process.start("/bin/bash");

  AsyncFw::Timer::single(500, [&process]() { process.input("echo 1234567890\n"); });
  AsyncFw::Timer::single(1000, [&process]() { process.input("_cmd_\n"); });
  AsyncFw::Timer::single(1000, [&process]() { process.input("ls /\n"); });
  AsyncFw::Timer::single(1500, [&process]() { process.input("exit\n"); });

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
```
