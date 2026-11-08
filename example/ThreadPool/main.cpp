/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

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

int main(int, char **) {
  AsyncFw::LogStream::setTimeFormat("%Y-%m-%d %H:%M:%S", true);

  Pool *threadPool = AsyncFw::Instance<AsyncFw::ThreadPool>::create<Pool>("ExampleThreadPool");
  logInfo() << threadPool->text();

  AsyncFw::Thread *_lt = AsyncFw::ThreadPool::instance()->createThread("LogThread");
  _lt->invoke([]() {
    AsyncFw::Instance<AsyncFw::Log>::create();  //create log instance in log thread _lt
  }, true);

  AsyncFw::Thread *_t = AsyncFw::ThreadPool::instance()->createThread("SyncExample");

  _t->invoke([_t]() {
    AsyncFw::ThreadPool::sync(_t, []() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      logInfo() << "sync run in thread" << ct->name() << ct->id();
    });
  });

  AsyncFw::ThreadPool::async([]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    logInfo() << "async run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async([]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    logInfo() << "async run in thread" << ct->name() << ct->id();
  }, []() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    logNotice() << "result without value, run in thread" << ct->name() << ct->id();
  });

  AsyncFw::ThreadPool::async([]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    logInfo() << "async run in thread" << ct->name() << ct->id();
    return 1;
  }, [](int r) {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    logNotice() << "result:" << r << "run in thread" << ct->name() << ct->id();
    AsyncFw::MainThread::exit(0);
  });

  logNotice() << "Start Application";

  AsyncFw::Thread::current()->started.connect([]() {
    std::vector<AsyncFw::AbstractThreadPool *> pools = AsyncFw::ThreadPool::pools();
    for (AsyncFw::AbstractThreadPool *pool : pools) {
      lsNotice() << pool->name();
      AsyncFw::LockData threads = pool->threads();
      for (const AsyncFw::AbstractThreadPool::Thread *thread : *threads) { lsInfo() << thread->name(); }
    }

    // Range-for over a temporary LockData: C++20 vs C++23.
    //
    // AbstractThread::threads() returns LockData<...>, which locks the
    // internal mutex in its constructor and unlocks it in its destructor.
    //
    // In C++20 ([stmt.ranged]/1) the range-expression is materialized to
    // `auto&& __range = *threads();` and the temporary LockData dies at the
    // end of that full-expression — BEFORE the first iteration. The loop body
    // then walks the vector with no lock held, racing against setId()/
    // clearId()/appendThread()/removeThread().
    //
    // In C++23 (P2718R0 "Wording for P2644R1: Fix for range-based for loop
    // with initializer") the temporary is lifetime-extended to the end of the
    // loop. The one-liner is safe there.
    //
    // The project builds with C++20 (CMAKE_CXX_STANDARD 20), so the safe form
    // is required today; the C++23 branch is kept for when the standard is
    // bumped, to drop the boilerplate.
#if defined(__cpp_range_based_for) && __cpp_range_based_for >= 202211L
    // C++23: temporary LockData is lifetime-extended for the whole loop.
    for (const AsyncFw::AbstractThread *thread : *AsyncFw::AbstractThread::threads()) { lsInfo() << thread->name(); }
#else
    // C++20: bind LockData to a named variable so the mutex stays held.
    std::string names;
    {
      auto locked = AsyncFw::AbstractThread::threads();
      for (const AsyncFw::AbstractThread *thread : *locked) { names += thread->name(); }
    }
    for (auto name : names) lsInfo() << name;
#endif
  });

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Application" << ret;
  return ret;
}
//! [snippet]
