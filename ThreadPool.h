#pragma once

#include "core/Thread.h"

#ifndef LS_NO_WARNING
  #include "core/console_msg.hpp"
#endif

#define ThreadPool_DEFAULT_WORK_THREADS 1

namespace AsyncFw {
class AbstractThreadPool {
public:
  static std::vector<AbstractThreadPool *> pools() { return pools_; }
  AbstractThreadPool(const std::string &);
  virtual ~AbstractThreadPool();
  virtual void quit();
  AbstractThread *thread() { return thread_; }
  std::string name() const { return name_; }
  AbstractThread::LockGuard threads(std::vector<AbstractThread *> **);

protected:
  class Thread : public AsyncFw::Thread {
  public:
    void destroy() override;

  protected:
    Thread(const std::string &name, AbstractThreadPool *);
    virtual ~Thread() override;
    AbstractThreadPool *pool;
  };

  virtual void appendThread(AbstractThread *);
  virtual void removeThread(AbstractThread *);
  std::vector<AbstractThread *> threads_;
  std::mutex mutex;
  AbstractThread *thread_;

private:
  struct Compare {
    bool operator()(const AbstractThread *t1, const AbstractThread *t2) const { return t1 < t2; }
  };
  inline static std::vector<AbstractThreadPool *> pools_;
  std::string name_;
};

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

  void removeThread(AbstractThread *) override;
  virtual void quit() override;

  Thread *getThread();

  template <typename M>
  static bool sync(AbstractThread *_t, M m) {
    return _t->invokeMethod(m, true);
  }
  template <typename M>
  static bool async(AbstractThread *_t, M m) {
    return _t->invokeMethod(m);
  }
  template <typename M>
  static bool async(M m) {
    return instance_->getThread()->invokeMethod(m);
  }
  template <typename M, typename R, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<std::is_void<T>::value, bool>::type async(AbstractThread *thread, M method, R result) {
    AbstractThread *_t = AbstractThread::currentThread();
#ifndef LS_NO_WARNING
    if (_t == thread) console_msg("thread and current thread are same: " + thread->name());
#endif
    return thread->invokeMethod([_t, method, result]() {
      method();
      _t->invokeMethod([result]() { result(); });
    });
  }
  template <typename M, typename R, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<!std::is_void<T>::value, bool>::type async(AbstractThread *thread, M method, R result) {
    AbstractThread *_t = AbstractThread::currentThread();
#ifndef LS_NO_WARNING
    if (_t == thread) console_msg("thread and current thread are same: " + thread->name());
#endif
    return thread->invokeMethod([_t, method, result]() { _t->invokeMethod([v = std::move(method()), result]() { result(v); }); });
  }
  template <typename M, typename R>
  static bool async(M m, R r) {
    return async(instance_->getThread(), m, r);
  }

private:
  inline static ThreadPool *instance_;
  std::vector<Thread *> workThreads_;
  int workThreadsSize;
};
}  // namespace AsyncFw
