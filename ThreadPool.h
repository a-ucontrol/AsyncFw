#pragma once

#include "core/Thread.h"

#define ThreadPool_DEFAULT_WORK_THREADS 2

namespace AsyncFw {
class ThreadPool : public AbstractThreadPool {
public:
  class Thread : public AbstractThreadPool::Thread {
    friend class ThreadPool;

  protected:
    using AbstractThreadPool::Thread::Thread;
    ~Thread();
  };

  static ThreadPool *instance() { return instance_; }
  ThreadPool(const std::string &, int = ThreadPool_DEFAULT_WORK_THREADS);
  ThreadPool(int workThreads = ThreadPool_DEFAULT_WORK_THREADS) : ThreadPool("ThreadPool", workThreads) {}
  ~ThreadPool();

  Thread *createThread(const std::string &name = {});

  virtual void quit() override;
  Thread *getThread();

  template <typename M>
  static bool sync(AbstractThread *_t, M m) {
    return Thread::invokeMethod(_t, m, true);
  }
  template <typename M>
  static decltype(auto) async(AbstractThread *_t, M m) {
    return Thread::invokeMethod(_t, m);
  }
  template <typename M>
  static decltype(auto) async(M m) {
    return Thread::invokeMethod(instance_->getThread(), m);
  }

private:
  inline static ThreadPool *instance_;
  std::vector<Thread *> workThreads_;
  int workThreadsSize;
};
}  // namespace AsyncFw
