/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "../core/Thread.h"
#include "Instance.h"

#define ThreadPool_DEFAULT_WORK_THREADS 1

namespace AsyncFw {
/*! \class AbstractThreadPool ThreadPool.h <AsyncFw/ThreadPool> \brief Abstract base class for creating and managing thread pools. */
class AbstractThreadPool {
public:
  /*! \class Thread \brief A specialized thread managed by an AbstractThreadPool. */
  class Thread : public AsyncFw::Thread {
  public:
    /*! \brief Gracefully destroys the thread, ensuring all tasks are flushed. */
    virtual void destroy();
    /*! \brief Constructs a pool-managed thread. */
    Thread(const std::string &name, AbstractThreadPool *);

  protected:
    /*! \brief Pointer to the parent thread pool that owns this thread. */
    AbstractThreadPool *pool;
  };

  /*! \brief Returns a list of all active thread pools in the application. */
  static std::vector<AbstractThreadPool *> pools();
  AbstractThreadPool(const std::string &);
  virtual ~AbstractThreadPool();
  virtual void quit();
  AbstractThread *thread() { return thread_; }
  /*! \brief Returns the identifier name of the thread pool. */
  std::string name() const;
  /*! \brief Thread-safely returns the internal list of threads managed by this pool. */
  AbstractThread::LockGuard threads(std::vector<AbstractThreadPool::Thread *> **);

protected:
  /*! \brief Registers a new thread into the pool. */
  virtual void appendThread(AbstractThreadPool::Thread *);
  /*! \brief Unregisters a thread from the pool. */
  virtual void removeThread(AbstractThreadPool::Thread *);
  std::vector<AbstractThreadPool::Thread *> threads_;
  std::mutex mutex;
  AbstractThread *thread_;

private:
  struct Private;
  Private &private_;
};

/*! \class ThreadPool ThreadPool.h <AsyncFw/ThreadPool> \brief Manages a set of reusable worker threads for concurrent task execution, eliminating the need to spawn a new thread for each individual task.
\brief By queuing tasks and recycling existing threads, it improves system performance, constrains resource allocation, and significantly reduces the overhead associated with thread creation and destruction.
\snippet ThreadPool/main.cpp snippet */
class ThreadPool : public AbstractThreadPool {
public:
  /*! \brief Synchronously invokes a function in the given thread's event loop (blocks the caller). */
  template <typename F>
  static bool sync(AbstractThread *_t, F f) {
    return _t->invoke(f, true);
  }
  /*! \brief Asynchronously invokes a function in the given thread's event loop. */
  template <typename F>
  static bool async(AbstractThread *_t, F f) {
    return _t->invoke(f);
  }
  /*! \brief Asynchronously invokes a function in the global/default thread pool. */
  template <typename F>
  static bool async(F f) {
    return instance_.value->getThread()->invoke(f);
  }
  /*! \brief Asynchronously invokes a void function in the target thread, and executes a result callback in the caller's thread upon completion. */
  template <typename F, typename R, typename T = std::invoke_result<F>::type>
  static typename std::enable_if<std::is_void<T>::value, bool>::type async(AbstractThread *thread, F function, R result) {
    AbstractThread *_t = AbstractThread::current();
    return thread->invoke([_t, function, result]() {
      function();
      _t->invoke([result]() { result(); });
    });
  }
  /*! \brief Asynchronously invokes a non-void function in the target thread, and forwards its return value to a result callback in the caller's thread upon completion. */
  template <typename F, typename R, typename T = std::invoke_result<F>::type>
  static typename std::enable_if<!std::is_void<T>::value, bool>::type async(AbstractThread *thread, F function, R result) {
    AbstractThread *_t = AbstractThread::current();
    return thread->invoke([_t, function, result]() { _t->invoke([v = std::move(function()), result]() { result(v); }); });
  }
  /*! \brief Asynchronously invokes a function in the global pool, returning the result to a callback in the caller's thread. */
  template <typename F, typename R>
  static bool async(F f, R r) {
    return async(instance_.value->getThread(), f, r);
  }

  /*! \brief Returns the instance of the global ThreadPool. */
  static inline ThreadPool *instance() { return instance_.value; }

  ThreadPool(const std::string &, int = ThreadPool_DEFAULT_WORK_THREADS);
  ThreadPool(int workThreads = ThreadPool_DEFAULT_WORK_THREADS) : ThreadPool("ThreadPool", workThreads) {}
  ~ThreadPool();

  /*! \brief Spawns and adds a new managed worker thread to the pool. */
  Thread *createThread(const std::string &name = {});

  void removeThread(AbstractThreadPool::Thread *) override;
  virtual void quit() override;

  /*! \brief Selects and returns the least loaded thread from the pool for task distribution. */
  AbstractThreadPool::Thread *getThread();

private:
  static Instance<ThreadPool> instance_;
  std::vector<AbstractThreadPool::Thread *> workThreads_;
  int workThreadsSize_;
};
}  // namespace AsyncFw
