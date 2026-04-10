/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Log>

class Pool : public AsyncFw::ThreadPool {  // example for AsyncFw::Instance<AsyncFw::ThreadPool>::create<Pool>
public:
  using AsyncFw::ThreadPool::ThreadPool;
  std::string text() { return "Pool"; }
};

int main(int argc, char *argv[]) {
  AsyncFw::LogStream::setTimeFormat("%Y-%m-%d %H:%M:%S", true);

  Pool *threadPool = AsyncFw::Instance<AsyncFw::ThreadPool>::create<Pool>("ExampeThreadPool");
  logInfo() << threadPool->text();

  AsyncFw::AbstractThread *_lt = AsyncFw::ThreadPool::instance()->createThread("LogThread");
  _lt->invokeMethod([]() { AsyncFw::Instance<AsyncFw::Log>::create(); }, true);  //create log instance in log thread _lt

  AsyncFw::AbstractThread *_t = AsyncFw::ThreadPool::instance()->createThread("SyncExample");

  AsyncFw::ThreadPool::sync(_t, []() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    logInfo() << "sync run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async([]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    logInfo() << "async run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async(
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        logInfo() << "async run in thread" << ct->name() << ct->id();
      },
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        logNotice() << "result without value, run in thread" << ct->name() << ct->id();
      });

  AsyncFw::ThreadPool::async(
      []() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        logInfo() << "async run in thread" << ct->name() << ct->id();
        return 1;
      },
      [](int r) {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
        logNotice() << "result:" << r << "run in thread" << ct->name() << ct->id();
        AsyncFw::MainThread::exit(0);
      });

  AsyncFw::AbstractThreadPool::Thread *_t1 = AsyncFw::ThreadPool::instance()->createThread("DestroyFromThreadExample");
  _t1->invokeMethod([_t1]() { _t1->destroy(); });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton" << ret;
  return ret;
}
//! [snippet]
