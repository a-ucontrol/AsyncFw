# AsyncFw
Async Framework

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
    if (++cnt == 10) AsyncFw::MainThread::instance()->exit(0);
  });

  timer2.timeout([]() { std::cout << std::chrono::system_clock::now() << " timer2 timeout" << std::endl; });

  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::instance()->exec();
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
      if (r == 2 && buf[0] == 'q') AsyncFw::MainThread::instance()->quit();
      std::cout << "stdin: " << buf;
    });
  } else {
    std::cout << "stdin is not connected to the terminal";
    return -1;
  }
  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::instance()->exec();
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
  AsyncFw::LogMinimal log;

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
        AsyncFw::MainThread::instance()->exit(0);
      });
    });
  });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::instance()->exec();

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
      if (cnt == 3) AsyncFw::MainThread::instance()->exit(0);
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
  AsyncFw::LogMinimal log;

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

  int ret = AsyncFw::MainThread::instance()->exec();

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
  AsyncFw::LogMinimal log;

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
        AsyncFw::MainThread::instance()->exit(0);
      });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::instance()->exec();

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
  AsyncFw::LogMinimal log;
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
    AsyncFw::MainThread::instance()->exit(0);
  });

  coro_task();

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::instance()->exec();

  logNotice() << "End Applicaiton";
  return ret;
}
```
