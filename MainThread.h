#pragma once

#include "core/Thread.h"

#ifndef _WIN32
  #define EXIT_ON_UNIX_SIGNAL
#endif

#ifdef USE_QAPPLICATION
  #include <map>
  #include <QTimerEvent>
  #include <QSocketNotifier>
  #include <QApplication>
#endif

#ifdef EXIT_ON_UNIX_SIGNAL
  #include <sys/eventfd.h>
#endif

namespace AsyncFw {
class MainThread : private Thread
#ifdef USE_QAPPLICATION
    ,
                   private QObject
#endif
{
public:
  static int exec() {
#ifndef USE_QAPPLICATION
    if (mainThread_.running()) return -1;
    mainThread_.Thread::exec();
    return mainThread_.code_;
#else
    mainThread_.state_ = 1;
    return qApp->exec();
#endif
  }
  static void exit(int code = 0) {
#ifdef EXIT_ON_UNIX_SIGNAL
    mainThread_.code_ = code;
    if (mainThread_.eventfd_ >= 0) eventfd_write(mainThread_.eventfd_, 1);
#else
  #ifndef USE_QAPPLICATION
    mainThread_.code_ = code;
    mainThread_.Thread::quit();
  #else
    qApp->exit(code);
  #endif
#endif
  }

private:
  MainThread() : Thread("Main") {
    setId();
#ifdef USE_QAPPLICATION
    AbstractThread::currentThread()->invokeMethod([this]() {
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        LockGuard lock = lockGuard();
        state_ = 2;
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
        if (eventfd_read(eventfd_, &_v) == 0) {
  #ifndef USE_QAPPLICATION
          Thread::quit();
  #else
        qApp->exit(code_);
  #endif
        }
      });
  #ifdef USE_QAPPLICATION
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
  }
#ifdef USE_QAPPLICATION
  struct Timer {
    int id;
    int qid;
    AbstractTask *task;
  };
  std::vector<Timer> timers;
  std::map<int, AbstractTask *> polls;

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
    LockGuard lock = lockGuard();
    for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
      if (it->id == id) {
        if (it->qid >= 0) QObject::killTimer(it->qid);
        if (it->task) delete it->task;
        timers.erase(it);
        return;
      }
    }
  }

  void timerEvent(QTimerEvent *e) override {
    int qid = e->timerId();
    for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
      if (it->qid == qid) {
        it->task->invoke();
        return;
      }
    }
  }

  bool invokeTask(AbstractTask *task) const override {
    LockGuard lock = lockGuard();
    if (state_ == 2) return false;
    QMetaObject::invokeMethod(
        const_cast<MainThread *>(this),
        [task]() {
          task->invoke();
          delete task;
        },
        Qt::QueuedConnection);
    return true;
  }

  struct Poll {
    Poll(MainThread *thread, int fd) : in(fd, QSocketNotifier::Read), out(fd, QSocketNotifier::Write), thread_(thread) {
      QObject::connect(&in, &QSocketNotifier::activated, [this, thread]() { task->invoke(AbstractThread::PollIn); });
      QObject::connect(&out, &QSocketNotifier::activated, [this, thread]() { task->invoke(AbstractThread::PollOut); });
    }
    ~Poll() { delete task; }
    QSocketNotifier in;
    QSocketNotifier out;
    AbstractThread *thread_;
    AbstractPollTask *task;
  };

  bool appendPollDescriptor(int fd, AbstractThread::PollEvents e, AbstractPollTask *task) override {
    for (const std::shared_ptr<Poll> &poll : notifiers)
      if (poll->in.socket() == fd) return false;
    std::shared_ptr<Poll> poll = std::make_shared<Poll>(this, fd);
    poll->in.setEnabled(e & AbstractThread::PollIn);
    poll->out.setEnabled(e & AbstractThread::PollOut);
    poll->task = task;
    notifiers.append(std::move(poll));
    return true;
  }

  void removePollDescriptor(int fd) override {
    for (const std::shared_ptr<Poll> &poll : notifiers)
      if (poll->in.socket() == fd) {
        notifiers.removeAll(poll);
        return;
      }
  }

  bool modifyPollDescriptor(int fd, PollEvents e) override {
    for (const std::shared_ptr<Poll> &poll : notifiers)
      if (poll->in.socket() == fd) {
        poll->in.setEnabled(e & AbstractThread::PollIn);
        poll->out.setEnabled(e & AbstractThread::PollOut);
        return true;
      }
    return false;
  }

  QList<std::shared_ptr<Poll>> notifiers;
#endif

#if defined EXIT_ON_UNIX_SIGNAL || !defined USE_QAPPLICATION
  int code_ = 0;
#endif
#ifdef EXIT_ON_UNIX_SIGNAL
  int eventfd_ = -1;
#endif
#ifdef USE_QAPPLICATION
  int state_ = 0;
#endif
  static MainThread mainThread_;
};
inline MainThread MainThread::mainThread_;
}  // namespace AsyncFw
