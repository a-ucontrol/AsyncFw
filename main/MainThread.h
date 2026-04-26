/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "../core/Thread.h"
#include "Instance.h"

#ifndef _WIN32
  #define EXIT_ON_UNIX_SIGNAL
#endif

#ifdef USE_QAPPLICATION
  #include <map>
  #include <QTimerEvent>
  #include <QSocketNotifier>
  #include <QCoreApplication>
#endif

#ifdef EXIT_ON_UNIX_SIGNAL
  #include <sys/eventfd.h>
#endif

namespace AsyncFw {
/*! \class MainThread MainThread.h <AsyncFw/MainThread> \brief The MainThread class.
 \brief Examlpe:
 \snippet snippet.dox MainThread
*/
class MainThread : private Thread
#ifdef USE_QAPPLICATION
    ,
                   private QObject
#endif
{
public:
  template <typename M>
  static void setExitTask(M method) {
    if (mt_.exitTask && !mt_.invokeMethod([_p = mt_.exitTask]() { delete _p; })) delete mt_.exitTask;
    mt_.exitTask = new Thread::Task(std::forward<M>(method));
  }
  static int exec() {
#ifndef USE_QAPPLICATION
    if (mt_.running()) return -1;
    mt_.Thread::exec();
    return mt_.code_;
#else
    mt_.state_ = 1;
    if (!qApp) return -1;
    return qApp->exec();
#endif
  }
  static void exit() {
#ifdef EXIT_ON_UNIX_SIGNAL
    if (mt_.eventfd_ >= 0) eventfd_write(mt_.eventfd_, 1);
#else
    (*mt_.exitTask)();
#endif
  }
  static void exit(int code) {
    mt_.code_ = code;
    exit();
  }
  static void quit() {
#ifndef USE_QAPPLICATION
    mt_.Thread::quit();
#else
    qApp->exit(mt_.code_);
#endif
  }

private:
  MainThread() : Thread("Main") {
    setId();
    setExitTask([]() { quit(); });
#ifdef USE_QAPPLICATION
    AbstractThread::currentThread()->invokeMethod([this]() {
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        finishedEvent();
        {  //lock scope
          LockGuard lock = lockGuard();
          state_ = 2;
        }
        qApp->processEvents();
      });
    });
#endif
#ifdef EXIT_ON_UNIX_SIGNAL
    eventfd_ = eventfd(0, EFD_NONBLOCK);
  #ifdef USE_QAPPLICATION
    AbstractThread::currentThread()->invokeMethod([this]() {
  #endif
      appendPollTask(eventfd_, AbstractThread::PollIn, [this](AbstractThread::PollEvents) {
        eventfd_t _v;
        if (eventfd_read(eventfd_, &_v) == 0) (*exitTask)();
      });
  #ifdef USE_QAPPLICATION
      startedEvent();
    });
  #endif
#endif
  }
  ~MainThread() {
#ifdef EXIT_ON_UNIX_SIGNAL
    if (eventfd_ >= 0) {
      removePollDescriptor(eventfd_);
      ::close(eventfd_);
    }
#endif
    delete exitTask;
    AbstractInstance::destroyValues();
    clearId();
#ifdef USE_QAPPLICATION
    for (const std::shared_ptr<Poll> &_n : notifiers) { delete _n->task; }
    for (const Timer &_t : timers) { delete _t.task; }
#endif
  }
#ifdef USE_QAPPLICATION
  struct Timer {
    int id;
    int qid;
    AbstractTask *task;
  };
  std::vector<Timer> timers;

  bool running() const override {
    LockGuard lock = lockGuard();
    return state_ == 1;
  }

  virtual int appendTimer(int msec, AbstractTask *task) override {
    int id = 0;
    int qid = -1;
    LockGuard lock = lockGuard();
    std::vector<Timer>::iterator it = timers.begin();
    for (; it != timers.end(); ++it, ++id)
      if (it->id != id) break;
    if (msec > 0) { qid = QObject::startTimer(msec); }
    timers.emplace(it, Timer {id, qid, task});
    return id;
  }

  virtual bool modifyTimer(int id, int msec) override {
    LockGuard lock = lockGuard();
    for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
      if (it->id == id) {
        if (it->qid >= 0) QObject::killTimer(it->qid);
        it->qid = (msec > 0) ? QObject::startTimer(msec) : -1;
        return true;
      }
    }
    return false;
  }

  virtual void removeTimer(int id) override {
    AbstractTask *_t = nullptr;
    {  //lock scope
      LockGuard lock = lockGuard();
      for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
        if (it->id == id) {
          if (it->qid >= 0) QObject::killTimer(it->qid);
          _t = new Task([p = it->task] { delete p; });
          timers.erase(it);
          break;
        }
      }
    }
    if (!invokeTask(_t)) {
      (*_t)();
      delete _t;
    }
  }

  void timerEvent(QTimerEvent *e) override {
    int qid = e->timerId();
    AbstractTask *_t = nullptr;
    {  //lock scope
      LockGuard lock = lockGuard();
      for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
        if (it->qid == qid) {
          _t = it->task;
          break;
        }
      }
    }
    if (_t) (*_t)();
  }

  bool invokeTask(AbstractTask *task) const override {
    LockGuard lock = lockGuard();
    if (state_ == 2) return false;
    QMetaObject::invokeMethod(
        const_cast<MainThread *>(this),
        [task]() {
          (*task)();
          delete task;
        },
        Qt::QueuedConnection);
    return true;
  }

  struct Poll {
    Poll(int fd) : in(fd, QSocketNotifier::Read), out(fd, QSocketNotifier::Write) {
      QObject::connect(&in, &QSocketNotifier::activated, [this]() { (*task)(AbstractThread::PollIn); });
      QObject::connect(&out, &QSocketNotifier::activated, [this]() { (*task)(AbstractThread::PollOut); });
    }

    QSocketNotifier in;
    QSocketNotifier out;
    AbstractPollTask *task;
  };

  bool appendPollDescriptor(int fd, AbstractThread::PollEvents e, AbstractPollTask *task) override {
    LockGuard lock = lockGuard();
    for (const std::shared_ptr<Poll> &poll : notifiers)
      if (poll->in.socket() == fd) return false;
    std::shared_ptr<Poll> poll = std::make_shared<Poll>(fd);
    poll->in.setEnabled(e & AbstractThread::PollIn);
    poll->out.setEnabled(e & AbstractThread::PollOut);
    poll->task = task;
    notifiers.append(std::move(poll));
    return true;
  }

  bool modifyPollDescriptor(int fd, PollEvents e) override {
    LockGuard lock = lockGuard();
    for (const std::shared_ptr<Poll> &poll : notifiers)
      if (poll->in.socket() == fd) {
        poll->in.setEnabled(e & AbstractThread::PollIn);
        poll->out.setEnabled(e & AbstractThread::PollOut);
        return true;
      }
    return false;
  }

  void removePollDescriptor(int fd) override {
    AbstractTask *_t = nullptr;
    {  //lock scope
      LockGuard lock = lockGuard();
      for (const std::shared_ptr<Poll> &poll : notifiers)
        if (poll->in.socket() == fd) {
          _t = new Task([p = poll->task] { delete p; });
          notifiers.removeAll(poll);
          break;
        }
    }
    if (!_t) return;
    if (!invokeTask(_t)) {
      (*_t)();
      delete _t;
    }
  }

  QList<std::shared_ptr<Poll>> notifiers;
#endif
  AbstractThread::AbstractTask *exitTask = nullptr;
  int code_ = 0;
#ifdef EXIT_ON_UNIX_SIGNAL
  int eventfd_ = -1;
#endif
#ifdef USE_QAPPLICATION
  int state_ = 0;
#endif
  static MainThread mt_;
};
inline MainThread MainThread::mt_;
}  // namespace AsyncFw
