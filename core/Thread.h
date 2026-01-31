#pragma once

#include <vector>
#include <condition_variable>

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

#if !defined LS_NO_ERROR
  #define AsyncFw_THREAD this
  #define checkCurrentThread() \
    if (std::this_thread::get_id() != AsyncFw_THREAD->id()) lsError() << "executed from different thread"
  #define checkDifferentThread() \
    if (std::this_thread::get_id() == AsyncFw_THREAD->id()) lsError() << "executed from own thread"
#else
  #define checkCurrentThread()
  #define checkDifferentThread()
#endif

//#define ABSTRACT_THREAD_LOCK_GUARD

namespace AsyncFw {
class LogStream;
class AbstractThread {
  friend class MainThread;
  friend LogStream &operator<<(LogStream &, const AbstractThread &);
  struct Private;

public:
  enum PollEvents : uint16_t { PollNo = 0, PollIn = POLLIN_, PollPri = POLLPRI_, PollOut = POLLOUT_, PollErr = POLLERR_, PollHup = POLLHUP_, PollNval = POLLNVAL_ };
  class AbstractTask {
  public:
    virtual void invoke() = 0;
    virtual ~AbstractTask() = default;
  };
  class AbstractPollTask {
  public:
    virtual void invoke(AbstractThread::PollEvents) = 0;
    virtual ~AbstractPollTask() = default;
  };
#ifndef ABSTRACT_THREAD_LOCK_GUARD
  using LockGuard = std::lock_guard<std::mutex>;
#else
  struct LockGuard {
    LockGuard(std::mutex &mutex) : mutex_(&mutex) { mutex_->lock(); }
    ~LockGuard() {
      if (mutex_) mutex_->unlock();
    }
    LockGuard(LockGuard &) = delete;
    LockGuard(LockGuard &&g) {
      this->mutex_ = g.mutex_;
      g.mutex_ = nullptr;
    }

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

  template <typename M>
  typename std::enable_if<std::is_void<typename std::invoke_result<M>::type>::value, bool>::type invokeMethod(M method, bool sync = false) const {
    if (!sync) {
      AbstractTask *_t = new Task(std::forward<M>(method));
      if (!invokeTask(_t)) {
        delete _t;
        return false;
      }
      return true;
    }
    if (std::this_thread::get_id() == id()) {
      method();
      return true;
    }
#if 1
    bool finished = false;
    AbstractTask *_t = new Task([_m = std::forward<M>(method), &finished, this]() mutable {
      _m();
      AbstractThread::LockGuard lock(mutex);
      finished = true;
      condition_variable.notify_all();
    });
    if (!invokeTask(_t)) {
      delete _t;
      return false;
    }
    std::unique_lock<std::mutex> lock(mutex);
    while (!finished) condition_variable.wait(lock);
#else  //ThreadSanitizer: data race
    std::atomic_flag finished;
    AbstractTask *_t = new Task([_m = std::forward<M>(method), &finished, this]() mutable {
      _m();
      finished.test_and_set();
      finished.notify_one();
    });
    if (!invokeTask(_t)) {
      delete _t;
      return false;
    }
    for (;;) {
      finished.wait(false);
      if (finished.test()) break;
    }
#endif
    return true;
  }

  static AbstractThread *currentThread();
  static AbstractThread::LockGuard threads(std::vector<AbstractThread *> **);

  virtual void startedEvent() {}
  virtual void finishedEvent() {}
  virtual void destroy() {}

  virtual bool running() const;
  virtual bool invokeTask(AbstractTask *) const;
  virtual int appendTimer(int, AbstractTask *);
  virtual bool modifyTimer(int, int);
  virtual void removeTimer(int);
  virtual bool appendPollDescriptor(int, PollEvents, AbstractPollTask *);
  virtual bool modifyPollDescriptor(int, PollEvents);
  virtual void removePollDescriptor(int);

  template <typename M>
  bool appendPollTask(int fd, PollEvents events, M method) {
    return appendPollDescriptor(fd, events, new PollTask(std::forward<M>(method)));
  }
  template <typename M>
  int appendTimerTask(int timeout, M method) {
    return appendTimer(timeout, new Task(std::forward<M>(method)));
  }

  void start();
  void requestInterrupt();
  bool interruptRequested() const;
  void waitInterrupted() const;
  void quit();
  void waitFinished() const;
  int workLoad() const;

  std::thread::id id() const;
  std::string name() const;

  LockGuard lockGuard() const { return LockGuard(mutex); }

protected:
  template <typename M>
  class Task : private AbstractTask {
    friend class AbstractThread;
    friend class AbstractSocket;

  private:
    Task(M &&method) : method(std::move(method)) {}
    virtual void invoke() override { method(); }
    M method;
  };

  template <typename M>
  class PollTask : public AbstractPollTask {
    friend class AbstractThread;

  private:
    PollTask(M &&method) : method(std::move(method)) {}
    virtual void invoke(AbstractThread::PollEvents _e) override { method(_e); }
    M method;
  };

  AbstractThread(const std::string &);
  virtual ~AbstractThread() = 0;
  mutable std::mutex mutex;
  mutable std::condition_variable condition_variable;

private:
  struct Compare {
    bool operator()(const AbstractThread *, const AbstractThread *) const;
    bool operator()(const AbstractThread *, std::thread::id) const;
  };
  static inline std::mutex list_mutex;
  static inline std::vector<AbstractThread *> list_threads;
  void setId();
  void exec();
  void wake() const;
  Private &private_;
};

class AbstractSocket;

class Thread : public AbstractThread {
  friend AbstractSocket;
  friend class ListenSocket;
  friend LogStream &operator<<(LogStream &, const Thread &);

public:
  static Thread *currentThread();
  Thread(const std::string & = "Thread");
  ~Thread() override;

protected:
  std::vector<AbstractSocket *> sockets_;
  void startedEvent() override;

private:
  struct Compare {
    bool operator()(const AbstractSocket *, const AbstractSocket *) const;
  };
  void appendSocket(AbstractSocket *);
  void removeSocket(AbstractSocket *);
};

constexpr AbstractThread::PollEvents operator|(AbstractThread::PollEvents e1, AbstractThread::PollEvents e2) { return static_cast<AbstractThread::PollEvents>(static_cast<uint8_t>(e1) | static_cast<uint8_t>(e2)); }
constexpr bool operator==(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
constexpr bool operator!=(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
}  // namespace AsyncFw
