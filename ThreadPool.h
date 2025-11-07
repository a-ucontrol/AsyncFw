#pragma once

#include "core/Thread.h"

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
  Thread *currentThread();
  ThreadPool(const std::string & = "Pool");
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
};
}  // namespace AsyncFw
