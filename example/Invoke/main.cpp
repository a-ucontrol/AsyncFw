#include <core/Thread.h>
#include <MainThread.h>
#include <Log.h>

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
