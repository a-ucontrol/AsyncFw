/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <thread>
#include <vector>

#define AsyncFw_STATIC_INIT_PRIORITY 65530

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

namespace AsyncFw {
class LogStream;
/*! \brief AsyncFw::AbstractThread manage the thread. */
class AbstractThread {
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
  using LockGuard = std::lock_guard<std::mutex>;
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
      processTasks();
      method();
      return true;
    }
    std::atomic_flag finished;
    AbstractTask *_t = new Task([&method, &finished]() mutable {
      method();
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
    return true;
  }
  template <typename M>
  bool appendPollTask(int fd, PollEvents events, M method) {
    return appendPollDescriptor(fd, events, new PollTask(std::forward<M>(method)));
  }
  template <typename M>
  int appendTimerTask(int timeout, M method) {
    return appendTimer(timeout, new Task(std::forward<M>(method)));
  }

  /*! \brief Returns a pointer to the AsyncFw::AbstractThread that manages the currently executing thread. */
  static AbstractThread *currentThread();
  /*! \brief Assigns a pointer to the list of all threads. \param std::vector<AbstractThread *> **pointer \return AbstractThread::LockGuard */
  static AbstractThread::LockGuard threads(std::vector<AbstractThread *> **);

  /*! \brief This call from thread when it starts executing. */
  virtual void startedEvent();
  /*! \brief This call from the thread when it finishing execution. */
  virtual void finishedEvent();

  /*! \brief Returns true if the managed thread is running. */
  virtual bool running() const;
  /*! \brief Runs a task in a managed thread. \param Poiner to AbstractTask \return True if the task is added to the queue */
  virtual bool invokeTask(AbstractTask *) const;
  virtual int appendTimer(int, AbstractTask *);
  virtual bool modifyTimer(int, int);
  virtual void removeTimer(int);
  virtual bool appendPollDescriptor(int, PollEvents, AbstractPollTask *);
  virtual bool modifyPollDescriptor(int, PollEvents);
  virtual void removePollDescriptor(int);

  void start();
  void requestInterrupt();
  bool interruptRequested() const;
  void waitInterrupted() const;
  void quit();
  void waitFinished() const;
  int workLoad() const;

  std::thread::id id() const;
  std::string name() const;

  LockGuard lockGuard() const;

protected:
  template <typename M>
  class Task : public AbstractTask {
  public:
    Task(M &&method) : method(std::move(method)) {}
    virtual void invoke() override { method(); }

  private:
    M method;
  };

  template <typename M>
  class PollTask : private AbstractPollTask {
    friend class AbstractThread;

  private:
    PollTask(M &&method) : method(std::move(method)) {}
    virtual void invoke(AbstractThread::PollEvents _e) override { method(_e); }
    M method;
  };

  AbstractThread(const std::string &);
  virtual ~AbstractThread() = 0;

  void setId();
  void clearId();
  void exec();

private:
  void processTasks() const;
  Private &private_;
};

class AbstractSocket;

/*! \brief AsyncFw::Thread thread with sockets. */
class Thread : public AbstractThread {
  friend AbstractSocket;
  friend class ListenSocket;
  friend LogStream &operator<<(LogStream &, const Thread &);

public:
  /*! \brief Returns a pointer to the AsyncFw::Thread that manages the currently executing thread. */
  static Thread *currentThread();
  Thread(const std::string & = "Thread");
  ~Thread() override;

protected:
  std::vector<AbstractSocket *> sockets_;
  void startedEvent() override;
  void finishedEvent() override;

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
