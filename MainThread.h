#pragma once

#include "core/Socket.h"

#ifndef _WIN32
  #define EXIT_ON_UNIX_SIGNAL
#endif

#ifdef USE_QAPPLICATION
  #include <map>
  #include <QTimerEvent>
  #include <QSocketNotifier>
  #include <QThread>
  #include <QApplication>
#endif

#ifdef EXIT_ON_UNIX_SIGNAL
  #include <sys/eventfd.h>
#endif

namespace AsyncFw {
inline class MainThread :
#ifdef USE_QAPPLICATION
    public SocketThread,
    private QObject
#else
    private SocketThread
#endif
{
public:
  static MainThread *instance() { return instance_; }
  MainThread() : SocketThread("Main") {
    AbstractThread::id_ = std::this_thread::get_id();
    instance_ = this;
#ifdef EXIT_ON_UNIX_SIGNAL
    eventfd_ = eventfd(0, EFD_NONBLOCK);
    invokeMethod([this]() {
      if (!running()) return;  //invoke in ~ExecLoopThread
      appendPollTask(eventfd_, AbstractThread::PollIn, [this](AbstractThread::PollEvents) {
        eventfd_t _v;
        (void)eventfd_read(eventfd_, &_v);
        if (_v == 1) {
  #ifndef USE_QAPPLICATION
          SocketThread::quit();
  #else
          qApp->exit(code_);
  #endif
        }
      });
    });
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

  virtual int appendTimer(int msec, AbstractTask *task) override {
    int id = 0;
    int qid = -1;
    std::unique_lock<MutexType> lock(mutex);
    std::vector<Timer>::iterator it = timers.begin();
    for (; it != timers.end(); ++it, ++id)
      if (it->id != id) break;
    if (msec > 0) { qid = QObject::startTimer(msec); }
    timers.emplace(it, Timer {id, qid, task});
    return id;
  }

  virtual bool modifyTimer(int id, int msec) override {
    std::unique_lock<MutexType> lock(mutex);
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
    std::unique_lock<MutexType> lock(mutex);
    for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
      if (it->id == id) {
        if (it->qid >= 0) QObject::killTimer(it->qid);
        if (it->task) delete it->task;
        timers.erase(it);
        return;
      }
    }
  }

private:
  void timerEvent(QTimerEvent *e) override {
    int qid = e->timerId();
    for (std::vector<Timer>::iterator it = timers.begin(); it != timers.end(); ++it) {
      if (it->qid == qid) {
        it->task->invoke();
        return;
      }
    }
  }

  bool invokeTask(AbstractTask *task) override {
    if (!this->QObject::thread()->isRunning()) return false;
    QMetaObject::invokeMethod(
        this,
        [task]() {
          task->invoke();
          delete task;
        },
        Qt::QueuedConnection);
    return true;
  }

  struct Poll {
    Poll(AbstractThread *thread, int fd) : in(fd, QSocketNotifier::Read), out(fd, QSocketNotifier::Write), thread_(thread) {
      QObject::connect(&in, &QSocketNotifier::activated, [this]() {
        static_cast<InternalPoolTask<AbstractThread::PollEvents> *>(task)->e_ = AbstractThread::PollIn;
        task->invoke();
      });
      QObject::connect(&out, &QSocketNotifier::activated, [this]() {
        static_cast<InternalPoolTask<AbstractThread::PollEvents> *>(task)->e_ = AbstractThread::PollOut;
        task->invoke();
      });
    }
    ~Poll() { delete task; }
    QSocketNotifier in;
    QSocketNotifier out;
    AbstractThread *thread_;
    AbstractTask *task;
  };

  bool appendPollDescriptor(int fd, AbstractThread::PollEvents e, AbstractTask *task) override {
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

public:
  int exec() {
#ifndef USE_QAPPLICATION
    SocketThread::exec();
    return code_;
#else
    return qApp->exec();
#endif
  }
  void exit(int code) {
    code_ = code;
    quit();
  }
  void quit() {
#ifndef USE_QAPPLICATION
  #ifdef EXIT_ON_UNIX_SIGNAL
    if (eventfd_ >= 0) eventfd_write(eventfd_, 1);
  #else
    SocketThread::quit();
  #endif
#else
    qApp->quit();
#endif
  }

private:
  inline static MainThread *instance_;
  int code_ = 0;
#ifdef EXIT_ON_UNIX_SIGNAL
  int eventfd_ = -1;
#endif
} mainThread_;
}  // namespace AsyncFw
