#pragma once

#include <condition_variable>

#include "FunctionConnector.h"
#include "AnyData.h"

#ifndef _WIN32
  #define POLLIN_ 0x001
  #define POLLPRI_ 0x002
  #define POLLOUT_ 0x004
  #define POLLERR_ 0x008
  #define POLLHUP_ 0x010
  #define POLLNVAL_ 0x020
#else
  #define POLLIN_ 0x0100
  #define POLLPRI_ 0x0400
  #define POLLOUT_ 0x0010
  #define POLLERR_ 0x0001
  #define POLLHUP_ 0x0002
  #define POLLNVAL_ 0x0004
#endif

#if !defined uC_NO_ERROR
  #define uC_THREAD this
  #define checkCurrentThread() \
    if (std::this_thread::get_id() != uC_THREAD->id()) ucError() << "executed from different thread"
  #define checkDifferentThread() \
    if (std::this_thread::get_id() == uC_THREAD->id()) ucError() << "executed from own thread"
#else
  #define checkCurrentThread()
  #define checkDifferentThread()
#endif

#define ABSTRACT_THREAD_LOCK_GUARD

namespace AsyncFw {
class AbstractTask {
  friend class AbstractThread;
  friend class ExecLoopThread;
  friend class MainThread;
  friend class AbstractSocket;

public:
  virtual void invoke() = 0;

protected:
  AbstractTask() = default;
  virtual ~AbstractTask();
};

class AbstractThread {
  friend class ExecLoopThread;
  friend class MainThread;
  friend class AbstractSocket;
  friend class AbstractThreadPool;
  struct Private;

public:
  using ConditionVariableType = std::condition_variable;
#ifndef ABSTRACT_THREAD_LOCK_GUARD
  using LockGuard = std::lock_guard<std::mutex>;
  /*struct LockGuard : public std::lock_guard<std::mutex> {
    using std::lock_guard<std::mutex>::lock_guard;
    LockGuard(AbstractThread *thread) : std::lock_guard<std::mutex>::lock_guard(thread->mutex) {}
  };*/
#else
  struct LockGuard {
    //LockGuard(AbstractThread *thread) : mutex_(&thread->mutex) { mutex_->lock(); }
    LockGuard(std::mutex &mutex) : mutex_(&mutex) { mutex_->lock(); }
    ~LockGuard() {
      if (mutex_) mutex_->unlock();
    }
    LockGuard(LockGuard &) = delete;
    //LockGuard(LockGuard &&g) {
    //  this->mutex_ = g.mutex_;
    //  g.mutex_ = nullptr;
    //}

  private:
    std::mutex *mutex_;
  };
#endif

  class Holder {
  public:
    void complete();
    void wait();

  private:
    AbstractThread *thread;
    bool waiting;
  };

  enum PollEvents : uint16_t { PollNo = 0, PollIn = POLLIN_, PollPri = POLLPRI_, PollOut = POLLOUT_, PollErr = POLLERR_, PollHup = POLLHUP_, PollInval = POLLNVAL_ };
  enum State : uint8_t { None = 0, WaitStarted, Running, WaitInterrupted, WaitFinished, Interrupted, Finalize, Finished };

  template <typename M>
  typename std::enable_if<std::is_void<typename std::invoke_result<M>::type>::value, bool>::type invokeMethod(M method, bool sync = false) {
    if (!sync) {
      AbstractTask *_t = new InternalTask(method);
      if (!invokeTask(_t)) {
        delete _t;
        return false;
      }
      return true;
    }
    if (std::this_thread::get_id() == id_) {
      method();
      return true;
    }
    bool finished = false;
    AbstractTask *_t = new InternalTask([method, &finished, this]() {
      method();
      finished = true;
      AbstractThread::LockGuard lock(mutex);
      condition_variable.notify_all();
    });
    if (!invokeTask(_t)) {
      delete _t;
      return false;
    }
    std::unique_lock<std::mutex> lock(mutex);
    while (!finished) condition_variable.wait(lock);
    return true;
  }

  static AbstractThread *currentThread();

  virtual void startedEvent() {}
  virtual void finishedEvent() {}
  virtual void destroy() {}

  virtual int appendTimer(int, AbstractTask *);
  virtual bool modifyTimer(int, int);
  virtual void removeTimer(int);
  virtual bool appendPollDescriptor(int, PollEvents, AbstractTask *);
  virtual bool modifyPollDescriptor(int, PollEvents);
  virtual void removePollDescriptor(int);

  template <typename M>
  bool appendPollTask(int fd, PollEvents events, M method) {
    return appendPollDescriptor(fd, events, new InternalPoolTask(method));
  }
  template <typename M>
  int appendTimerTask(int timeout, M method) {
    return appendTimer(timeout, new InternalTask(method));
  }

  bool running();
  void requestInterrupt();
  bool interruptRequested() const;
  void waitInterrupted() const;
  void quit();
  void waitFinished() const;
  int queuedTasks() const;

  std::thread::id id() const { return id_; }
  std::string name() const { return name_; }

  void lock() { mutex.lock(); }
  void unlock() { mutex.unlock(); }
  LockGuard lockGuard() { return LockGuard(mutex); }

protected:
  template <typename M, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<std::is_void<T>::value, FunctionConnector<> &>::type invokeMethod(AbstractThread *thread, M method) {
    FunctionConnector<> *fc
#ifndef __clang_analyzer__
        = new FunctionConnector<>()
#endif
        ;
    AbstractThread::currentThread()->invokeMethod([thread, fc, method]() {
      if (!thread->invokeMethod([fc, method]() {
            method();
            (*fc)();
            delete fc;
          }))
        delete fc;
    });
    return *fc;
  }
  template <typename M, typename T = std::invoke_result<M>::type>
  static typename std::enable_if<!std::is_void<T>::value, FunctionConnector<T> &>::type invokeMethod(AbstractThread *thread, M method) {
    FunctionConnector<T> *fc
#ifndef __clang_analyzer__
        = new FunctionConnector<T>()
#endif
        ;
    AbstractThread::currentThread()->invokeMethod([thread, fc, method]() {
      if (!thread->invokeMethod([fc, method]() {
            (*fc)(std::move(method()));
            delete fc;
          }))
        delete fc;
    });
    return *fc;
  }

  template <typename M>
  class InternalTask : private AbstractTask {
    friend class AbstractThread;
    friend class ExecLoopThread;
    friend class AbstractSocket;

  private:
    InternalTask(M method) : method(method) {}
    virtual void invoke() override { method(); }
    M method;
  };

  template <typename M>
  class InternalPoolTask : public AbstractTask {
    friend class AbstractThread;
    friend class AbstractSocket;
    friend class MainThread;

  public:
    InternalPoolTask(M method) : method(method) {}

  private:
    virtual void invoke() override { method(e_); }
    AbstractThread::PollEvents e_;
    M method;
  };

  AbstractThread(const std::string &);
  virtual ~AbstractThread() = 0;
  virtual bool invokeTask(AbstractTask *);
  void start();
  mutable std::mutex mutex;
  mutable ConditionVariableType condition_variable;

private:
  struct Compare {
    bool operator()(const AbstractThread *, const AbstractThread *) const;
    bool operator()(const AbstractThread *, std::thread::id) const;
  };
  static inline std::mutex list_mutex;
  static inline std::vector<AbstractThread *> threads_;
  void changeId(std::thread::id);
  void exec();
  void wake();
  Private *private_;
  int state = None;
  std::thread::id id_;
  std::string name_;
};

class AbstractSocket;

class Thread : public AbstractThread {
  friend AbstractSocket;

public:
  Thread(const std::string & = "AsyncFw::Thread");
  ~Thread() override;

protected:
  std::vector<AbstractSocket *> sockets_;
  void startedEvent() override;
  void appendSocket(AbstractSocket *);
  void removeSocket(AbstractSocket *);

private:
  struct Compare {
    bool operator()(const AbstractSocket *, const AbstractSocket *) const;
  };
};

template <typename M>
class ThreadTask;

class AbstractThreadTask : public AbstractTask, public AnyData {
public:
  virtual bool isRunning() = 0;
  template <typename _M>
  static AbstractThreadTask *createTask(_M method, AbstractThread *thread = nullptr) {
    return new ThreadTask(method, thread);
  }
};

template <typename M>
class ThreadTask : public AbstractThreadTask {
  friend class AbstractThreadTask;
  class run {
    friend class ThreadTask;
    run(std::atomic_bool *ptr) : r_(ptr) {}
    std::atomic_bool *r_;

  public:
    ~run() { *r_ = false; }
  };

public:
  void invoke() override {
    running = true;
    if (!thread) {
      method(&data_);
      running = false;
      return;
    }
    std::shared_ptr<run> r = std::shared_ptr<run>(new run(&running));
    thread->invokeMethod([this, r]() { method(&data_); });
  }
  bool isRunning() override { return running; }

protected:
  ThreadTask(M method, AbstractThread *thread) : method(method), thread(thread) {}

private:
  M method;
  AbstractThread *thread;
  std::atomic_bool running = false;
};

class AbstractThreadPool {
public:
  static std::vector<AbstractThreadPool *> pools() { return pools_; }
  static AbstractThread::LockGuard threads(std::vector<AbstractThread *> **, AbstractThreadPool * = nullptr);
  AbstractThreadPool(const std::string &, AbstractThread * = nullptr);
  virtual ~AbstractThreadPool();
  virtual void quit();
  AbstractThread *thread() { return thread_; }
  std::string name() const { return name_; }

protected:
  class Thread : public AsyncFw::Thread {
  public:
    void quit();

  protected:
    Thread(const std::string &name, AbstractThreadPool *, bool = true);
    virtual ~Thread() override = 0;
    AbstractThreadPool *pool;
    void destroy() override;
  };

  void appendThread(AbstractThread *);
  void removeThread(AbstractThread *);
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

constexpr AbstractThread::PollEvents operator|(AbstractThread::PollEvents e1, AbstractThread::PollEvents e2) { return static_cast<AbstractThread::PollEvents>(static_cast<uint8_t>(e1) | static_cast<uint8_t>(e2)); }
constexpr bool operator==(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
}  // namespace AsyncFw
