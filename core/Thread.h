#pragma once

#include <future>
#include "FunctionConnector.h"
#include "AnyData.h"

#ifndef _WIN32
  #define POLLIN_   0x001
  #define POLLPRI_  0x002
  #define POLLOUT_  0x004
  #define POLLERR_  0x008
  #define POLLHUP_  0x010
  #define POLLNVAL_ 0x020
#else
  #define POLLIN_   0x0100
  #define POLLPRI_  0x0400
  #define POLLOUT_  0x0010
  #define POLLERR_  0x0001
  #define POLLHUP_  0x0002
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

namespace AsyncFw {
class AbstractTask {
  friend class AbstractThread;
  friend class ExecLoopThread;
  friend class AbstractSocket;
  friend class MainThread;

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
  friend class AbstractLog;
  friend class AbstractThreadPool;
//  friend struct CoroutineTask;

public:
  using MutexType = std::mutex;
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
    std::promise<void> promise;
    std::future<void> future = promise.get_future();
    AbstractTask *_t         = new InternalSyncTask(method, &promise);
    if (!invokeTask(_t)) {
      delete _t;
      return false;
    }
    future.get();
    return true;
  }

  static AbstractThread *thread(std::thread::native_handle_type);
  static AbstractThread *currentThread() { return thread(0); }

  virtual int appendTimer(int, AbstractTask *);
  virtual bool modifyTimer(int, int);
  virtual void removeTimer(int);
  virtual bool appendPollDescriptor(int, PollEvents, AbstractTask *);
  virtual bool modifyPollDescriptor(int, PollEvents);
  virtual void removePollDescriptor(int);
  bool running();
  std::thread::id id() const { return id_; }

  template <typename M>
  bool appendPollTask(int fd, PollEvents events, M method) {
    return appendPollDescriptor(fd, events, new InternalPoolTask(method));
  }
  template <typename M>
  int appendTimerTask(int timeout, M method) {
    return appendTimer(timeout, new InternalTask(method));
  }

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
  class InternalSyncTask : private InternalTask<M> {
    friend class AbstractThread;

  private:
    InternalSyncTask(M method, std::promise<void> *promise) : InternalTask<M>(method), promise(promise) {}
    ~InternalSyncTask() override { promise->set_value(); }
    std::promise<void> *promise;
  };

  template <typename M>
  class InternalPoolTask : public AbstractTask {
    friend class ExecLoopThread;
    friend class MainThread;
    friend class AbstractSocket;

  public:
    InternalPoolTask(M method) : method(method) {}

  private:
    virtual void invoke() override { method(e_); }
    AbstractThread::PollEvents e_;
    M method;
  };

  AbstractThread();
  virtual ~AbstractThread()               = 0;
  virtual bool invokeTask(AbstractTask *) = 0;
  virtual void destroy()                  = 0;
  mutable MutexType mutex;

private:
  int state = None;
  static inline MutexType list_mutex;
  static inline std::vector<AbstractThread *> threads;
  std::thread::id id_;
};

class ExecLoopThread : public AbstractThread {
  friend class DataArraySocket;
  friend class MainThread;
  friend struct CoroutineTask;
  using ConditionVariableType = std::condition_variable;
  struct Private;

public:
  ExecLoopThread(const std::string & = {});
  ~ExecLoopThread() override;
  int appendTimer(int, AbstractTask *) override;
  bool modifyTimer(int, int) override;
  void removeTimer(int) override;
  bool appendPollDescriptor(int, PollEvents = PollIn, AbstractTask * = nullptr) override;
  bool modifyPollDescriptor(int, PollEvents) override;
  void removePollDescriptor(int) override;
  void destroy() override;

  void start();
  void quit();

  std::string name() const;
  void setName(const std::string &);

  bool interruptRequested() const;
  void requestInterrupt();
  void waitInterrupted() const;
  void waitFinished() const;
  int queuedTasks() const;

protected:
  class Holder {
  public:
    void complete();
    void wait();

  private:
    ExecLoopThread *thread;
    bool waiting;
  };
  bool invokeTask(AbstractTask *) override;
  virtual void startedEvent() {}
  virtual void finishedEvent() {}
  mutable ConditionVariableType condition_variable;

private:
  void exec();
  void wake();
  void process_tasks();
  void process_polls();
  void process_timers();
  Private *private_;
};

class AbstractSocket;

class SocketThread : public ExecLoopThread {
  friend AbstractSocket;
  friend class DataArraySocket;

public:
  SocketThread(const std::string & = {});
  ~SocketThread() override;

protected:
  std::vector<AbstractSocket *> sockets_;
  void startedEvent() override;
  void appendSocket(AbstractSocket *);
  void removeSocket(AbstractSocket *);

private:
  struct Compare {
    bool operator()(const AbstractSocket *, int) const;
    bool operator()(int, const AbstractSocket *) const;
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
  friend class DataArrayAbstractTcp;

public:
  static int threadCount(AbstractThreadPool * = nullptr);
  static std::vector<AbstractThreadPool *> pools() { return pools_; }
  AbstractThreadPool(AbstractThread * = nullptr);
  virtual ~AbstractThreadPool();
  virtual void quit();
  AbstractThread *thread() { return thread_; }

protected:
  class Thread : public SocketThread {
    friend class AbstractThreadPool;

  public:
    void quit();

  protected:
    Thread(AbstractThreadPool *, bool = true);
    virtual ~Thread() override = 0;
    AbstractThreadPool *pool;
    void destroy() override;
  };

  void removeThread(AbstractThread *);
  std::vector<AbstractThread *> threads_;
  AbstractThread::MutexType mutex;
  AbstractThread *thread_;

private:
  inline static std::vector<AbstractThreadPool *> pools_;
};

constexpr AbstractThread::PollEvents operator|(AbstractThread::PollEvents f1, AbstractThread::PollEvents f2) { return static_cast<AbstractThread::PollEvents>(static_cast<uint8_t>(f1) | static_cast<uint8_t>(f2)); }
constexpr AbstractThread::PollEvents operator==(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
}  // namespace AsyncFw

#ifndef _uC_BUILD_LIBS_
  #include "main-thread.hpp"
#endif
