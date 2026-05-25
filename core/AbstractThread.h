/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include "invocable.hpp"

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
/*! \class AbstractThread AbstractThread.h <AsyncFw/AbstractThread> \brief The AbstractThread class provides the base functionality for thread management. */
class AbstractThread {
  friend class Thread;
  friend LogStream &operator<<(LogStream &, const AbstractThread &);

public:
  /*! \enum PollEvents \brief Bitmask constants mirroring system poll definitions (POLLIN, POLLOUT, etc.) for I/O monitoring. */
  enum PollEvents : uint16_t { PollNo = 0, PollIn = POLLIN_, PollPri = POLLPRI_, PollOut = POLLOUT_, PollErr = POLLERR_, PollHup = POLLHUP_, PollNval = POLLNVAL_ };
  /*! \brief The LockGuard type. */
  using LockGuard = std::lock_guard<std::mutex>;
  /*! \brief The AbstractTask type. */
  using AbstractTask = Invocable<void()>::Abstract;
  /*! \brief The AbstractPollTask type. */
  using AbstractPollTask = Invocable<void(AbstractThread::PollEvents)>::Abstract;

  /*! \brief The Holder class. */
  class Holder {
  public:
    void complete();
    /*! \brief Runs nested exec() and wait for it completed. */
    void wait();

  private:
    AbstractThread *thread;
    bool waiting;
  };

  /*! \brief Runs a function in a managed thread. \param function runs function \param sync blocking wait if true \return True if the function is added to the queue */
  template <typename F>
  typename std::enable_if<std::is_void<typename std::invoke_result<F>::type>::value, bool>::type invoke(F function, bool sync = false) const {
    if (!sync) {
      AbstractTask *_t = new Invocable<void()>::Function(std::forward<F>(function));
      if (!invokeTask(_t)) {
        delete _t;
        return false;
      }
      return true;
    }
    if (std::this_thread::get_id() == id()) {
      processTasks();
      function();
      return true;
    }
    std::atomic_flag finished;
    AbstractTask *_t = new Invocable<void()>::Function([&function, &finished]() {
      function();
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
  /*! \brief Append poll task. \param fd file descriptor \param events watch events \param function task function \return True if the task added */
  template <typename F>
  bool appendPollTask(int fd, PollEvents events, F function) {
    return appendPollDescriptor(fd, events, new Invocable<void(PollEvents)>::Function(std::forward<F>(function)));
  }
  /*! \brief Append timer task. \param ms timeout in milliseconds \param function task function \return timer id if the task added or value less than zero */
  template <typename F>
  int appendTimerTask(int timeout, F function) {
    return appendTimer(timeout, new Invocable<void()>::Function(std::forward<F>(function)));
  }

  /*! \brief Returns a pointer to the AsyncFw::AbstractThread that manages the currently executing thread. */
  static AbstractThread *current();
  /*! \brief Assigns a pointer to the list of all threads. \param list pointer to the list of threads \return AbstractThread::LockGuard */
  static AbstractThread::LockGuard threads(std::vector<AbstractThread *> **);

  /*! \brief This call from thread when it starts executing. */
  virtual void startedEvent();
  /*! \brief This call from the thread when it finishing execution. */
  virtual void finishedEvent();

  /*! \brief Returns true if the managed thread is running. */
  virtual bool running() const;
  /*! \brief Runs a task in a managed thread. \param task poiner to AbstractTask \return True if the task is added to the queue */
  virtual bool invokeTask(AbstractTask *) const;
  /*! \brief Append timer. \param ms timeout in milliseconds \param task pointer to AbstractTask \return timer id if timer added or value less than zero */
  virtual int appendTimer(int, AbstractTask *);
  /*! \brief Modify timer. \param id timer id \param ms timeout in milliseconds \return True if the timer modified */
  virtual bool modifyTimer(int, int);
  /*! \brief Remove timer. \param id timer id */
  virtual void removeTimer(int);
  /*! \brief Append poll descriptor. \param fd file descriptor \param events watch events \param task pointer to AbstractPollTask \return True if the poll descriptor added */
  virtual bool appendPollDescriptor(int, PollEvents, AbstractPollTask *);
  /*! \brief Modify poll descriptor. \param fd file descriptor \param events watch events \return True if the poll descriptor modified */
  virtual bool modifyPollDescriptor(int, PollEvents);
  /*! \brief Remove poll descriptor. \param fd file descriptor */
  virtual void removePollDescriptor(int);

  /*! \brief Create a managed thread and run exec(). */
  void start();
  /*! \brief Request the interruption of the thread.*/
  void requestInterrupt();
  /*! \brief Returns true if an interrupt is requested.*/
  bool interruptRequested() const;
  /*! \brief Wait for thread interrupted. This means that requestInterrupt() has been called and all queue tasks have completed. */
  void waitInterrupted() const;
  /*! \brief Tells the thread's exec() to exit. */
  void quit();
  /*! \brief Wait for the thread's exec() function finished. */
  void waitFinished() const;
  /*! \brief Returns the number of tasks in the queue, plus one if there are running task. */
  int workLoad() const;

  /*! \brief Returns unique identifier of managed thread. */
  std::thread::id id() const;
  /*! \brief Returns name of managed thread */
  std::string name() const;

  /*! \brief Locks the managed thread and returns a LockGuard variable. The thread is unblocked after this variable is destroyed. */
  LockGuard lockGuard() const;

protected:
  /*! \brief Constructs a thread. \param name thread name */
  AbstractThread(const std::string &);
  virtual ~AbstractThread() = 0;

  void setId();
  void clearId();
  /*! \brief Run manage loop */
  void exec();

private:
  void processTasks() const;
  struct Private;
  Private &private_;
};

constexpr AbstractThread::PollEvents operator|(AbstractThread::PollEvents e1, AbstractThread::PollEvents e2) { return static_cast<AbstractThread::PollEvents>(static_cast<uint8_t>(e1) | static_cast<uint8_t>(e2)); }
constexpr bool operator==(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
constexpr bool operator!=(AbstractThread::PollEvents, AbstractThread::PollEvents) = delete;
}  // namespace AsyncFw
