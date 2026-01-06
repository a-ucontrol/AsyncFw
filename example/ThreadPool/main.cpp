#include <ThreadPool.h>
#include <MainThread.h>
#include <Log.h>

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
        AsyncFw::MainThread::exit(0);
      });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton" << ret;
  return ret;
}
