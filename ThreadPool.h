#pragma once

#include "core/Thread.h"
#include "instance.hpp"

#define ThreadPool_DEFAULT_WORK_THREADS 1

namespace AsyncFw {
class AbstractThreadPool {
public:
  class Thread : public AsyncFw::Thread {
  public:
    virtual void destroy();
    Thread(const std::string &name, AbstractThreadPool *);

  protected:
    AbstractThreadPool *pool;
  };

  struct List : public std::vector<AbstractThreadPool *> {
    friend AbstractThreadPool;
    static void destroy();

  private:
    ~List();
  };

  static std::vector<AbstractThreadPool *> pools() { return pools_; }
  AbstractThreadPool(const std::string &);
  virtual ~AbstractThreadPool();
  virtual void quit();
  AbstractThread *thread() { return thread_; }
  std::string name() const { return name_; }
  AbstractThread::LockGuard threads(std::vector<AbstractThreadPool::Thread *> **);

protected:
  virtual void appendThread(AbstractThreadPool::Thread *);
  virtual void removeThread(AbstractThreadPool::Thread *);
  std::vector<AbstractThreadPool::Thread *> threads_;
  std::mutex mutex;
  AbstractThread *thread_;

private:
  struct Compare {
    bool operator()(const AbstractThread *t1, const AbstractThread *t2) const { return t1 < t2; }
  };
  inline static List pools_;
  std::string name_;
};

class ThreadPool : public AbstractThreadPool {
public:
  static ThreadPool *instance() { return instance_.value; }

  ThreadPool(const std::string &, int = ThreadPool_DEFAULT_WORK_THREADS);
  ThreadPool(int workThreads = ThreadPool_DEFAULT_WORK_THREADS) : ThreadPool("ThreadPool", workThreads) {}
  ~ThreadPool();

  Thread *createThread(const std::string &name = {});

  void removeThread(AbstractThreadPool::Thread *) override;
  virtual void quit() override;

  AbstractThreadPool::Thread *getThread();

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
    return instance_.value->getThread()->invokeMethod(m);
  }
  template <typename M, typename R, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<std::is_void<T>::value, bool>::type async(AbstractThread *thread, M method, R result) {
    AbstractThread *_t = AbstractThread::currentThread();
    return thread->invokeMethod([_t, method, result]() {
      method();
      _t->invokeMethod([result]() { result(); });
    });
  }
  template <typename M, typename R, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<!std::is_void<T>::value, bool>::type async(AbstractThread *thread, M method, R result) {
    AbstractThread *_t = AbstractThread::currentThread();
    return thread->invokeMethod([_t, method, result]() { _t->invokeMethod([v = std::move(method()), result]() { result(v); }); });
  }
  template <typename M, typename R>
  static bool async(M m, R r) {
    return async(instance_.value->getThread(), m, r);
  }

private:
  inline static AsyncFw::Instance<ThreadPool> instance_;
  std::vector<AbstractThreadPool::Thread *> workThreads_;
  int workThreadsSize;
};
}  // namespace AsyncFw
