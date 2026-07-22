/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include <condition_variable>
#include <queue>
#include "AbstractThread.h"
#include "LogStream.h"

#if 0  // 1 - for debug only
  //#define PIPE_WAKE
  //#define EVENTFD_WAKE
  //#define SOCKET_PAIR_WAKE
  #define SOCKET_CLOSE_WAKE
  //#define IO_URING_WAKE
  #define POLL_WAIT
  //#define EPOLL_WAIT
  //#define EPOLL_EDGE_TRIGGERED
  //#define IO_URING_WAIT
#else
  #ifdef __linux
    #ifndef IO_URING_WAIT
      #define EVENTFD_WAKE
      #define EPOLL_WAIT
    //#define EPOLL_EDGE_TRIGGERED
    #else
      #ifndef IO_URING_WAKE
        #define EVENTFD_WAKE
      #endif
    #endif
  #elif defined _WIN32
    #define SOCKET_PAIR_WAKE
    #define POLL_WAIT
  #else
    #define PIPE_WAKE
    #define POLL_WAIT
  #endif
#endif

#if !defined LS_NO_ERROR
  #define AsyncFw_THREAD this
#endif

#ifdef EXTEND_THREAD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
  #define trace_if(x) \
    if (x) LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace() \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
  #define trace_if(x) \
    if constexpr (0) LogStream()
#endif

#ifndef _WIN32
  #ifdef EVENTFD_WAKE
    #include <sys/eventfd.h>
  #elif defined SOCKET_PAIR_WAKE || defined SOCKET_CLOSE_WAKE
    #include <sys/socket.h>
    #include <netinet/in.h>
  #endif
  #ifdef POLL_WAIT
    #include <sys/poll.h>
  #elif defined EPOLL_WAIT
    #include <sys/epoll.h>
    #define EPOLL_WAIT_EVENTS 32
  #elif defined IO_URING_WAIT
    #include <liburing.h>
    #define IO_URING_QUEUE_SIZE 256
  #endif
  #include <unistd.h>
  #define close_fd ::close
  #define read_fd(fd, ptr, size) ::read(fd, ptr, size)
  #define write_fd(fd, ptr, size) ::write(fd, ptr, size)
#else
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <fcntl.h>
  #define poll(ptr, size, timeout) WSAPoll(ptr, size, timeout)
  #define close_fd ::closesocket
  #define read_fd(fd, ptr, size) ::recv(fd, ptr, size, 0)
  #define write_fd(fd, ptr, size) ::send(fd, ptr, size, 0)
#endif

#ifdef POLL_WAIT
  #define WAKE_FD fds_[0].fd
#elif defined EVENTFD_WAKE && (defined EPOLL_WAIT || defined IO_URING_WAIT)
  #define WAKE_FD wake_task.fd
#endif

#define LOG_THREAD_NAME ('(' + private_.name + ')')
#define QUEUE_TASKS_OVERLOAD_SIZE 128

#include "console_msg.hpp"

using namespace AsyncFw;

struct AbstractThread::Private {
  enum State : uint8_t { None = 0, WaitStarted = 0x01, Running = 0x02, WaitInterrupted = 0x04, Interrupted = 0x08, WaitFinished = 0x10, Finished = 0x20 };
  struct Timer {
    int id;
    std::chrono::milliseconds timeout;
    std::chrono::time_point<std::chrono::steady_clock> expire;
    AbstractTask *task = nullptr;
  };

  struct PollTask {
    ~PollTask() {
      trace() << LogStream::Color::Yellow << "destroy" << this << fd;
      delete task;
    }
    int fd;
    AbstractPollTask *task;
  };

  std::mutex mutex;
  std::condition_variable condition_variable;

  std::queue<AbstractTask *> tasks;
  std::vector<Timer> timers;
  std::chrono::time_point<std::chrono::steady_clock> wakeup = std::chrono::time_point<std::chrono::steady_clock>::max();
  bool wake_ = true;

  std::vector<PollTask *> poll_tasks;

#ifdef PIPE_WAKE
  int pipe[2];
  #define WAKE_FD_WRITE pipe[1]
#elif defined EVENTFD_WAKE
  #define WAKE_FD_WRITE WAKE_FD
#elif defined SOCKET_PAIR_WAKE
  int socketpair_write;
  #define WAKE_FD_WRITE socketpair_write
#elif defined SOCKET_CLOSE_WAKE
  #define WAKE_FD_WRITE WAKE_FD
#endif

#ifdef POLL_WAIT
  struct update_pollfd {
    int fd;
    short int events;
    AbstractPollTask *task;
    int8_t action;
  };
  std::vector<struct update_pollfd> update_pollfd;
  std::vector<pollfd> fds_;
  std::vector<AbstractPollTask *> fdts_;
#elif defined EPOLL_WAIT
  int epoll_fd;
  PollTask wake_task;
#elif defined IO_URING_WAIT
  struct io_uring ring;
  #ifndef IO_URING_WAKE
  PollTask wake_task;
  #endif
#endif

  struct ProcessTimerTask {
    int id;
    AbstractTask *task;
  };
  struct ProcessPollTask {
    int fd;
#ifdef POLL_WAIT
    short int events;
#elif defined EPOLL_WAIT
    uint32_t events;
#elif defined IO_URING_WAIT
    int32_t events;
#endif
    AbstractPollTask *task;
  };

  void wake();
  void process_tasks();
  void process_polls();
  void process_timers();
  int state = None;
  std::thread::id id;
  std::string name;
  std::queue<AbstractTask *> process_tasks_;
  std::queue<ProcessTimerTask> process_timer_tasks_;
  std::deque<ProcessPollTask> process_poll_tasks_;
  int nested_ = 0;

  struct Compare {
    bool operator()(const AbstractThread *, const AbstractThread *) const;
    bool operator()(const AbstractThread *t, std::thread::id id) const { return t->private_.id < id; }
    bool operator()(const Timer &t, int id) const { return t.id < id; }
    bool operator()(const PollTask *d, int fd) const { return d->fd < fd; }
    bool operator()(const ProcessPollTask d, int fd) const { return d.fd < fd; }
#ifdef POLL_WAIT
    bool operator()(const pollfd pfd, int fd) const { return static_cast<int>(pfd.fd) < fd; }
#endif
  };

  static inline struct List : public std::vector<AbstractThread *> {
    ~List();
    std::mutex mutex;
  } list __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 1)));
};

bool AbstractThread::Private::Compare::operator()(const AbstractThread *t1, const AbstractThread *t2) const {
  if (t1->private_.id != t2->private_.id) return t1->private_.id < t2->private_.id;
  return t1 < t2;
}

void AbstractThread::Private::process_tasks() {
  for (; !process_tasks_.empty();) {
    AbstractTask *_t = process_tasks_.front();
    process_tasks_.pop();
    (*_t)();
    delete _t;
  }
}

void AbstractThread::Private::process_polls() {
  for (; !process_poll_tasks_.empty();) {
    Private::ProcessPollTask _pt = process_poll_tasks_.front();
    process_poll_tasks_.pop_front();
    (*_pt.task)(static_cast<AbstractThread::PollEvents>(_pt.events));
  }
}

void AbstractThread::Private::process_timers() {
  for (; !process_timer_tasks_.empty();) {
    Private::ProcessTimerTask _tt = process_timer_tasks_.front();
    process_timer_tasks_.pop();
    (*_tt.task)();
  }
}

void AbstractThread::Waiter::complete() {
  if (!thread_) {
    lsWarning() << "not waiting";
    return;
  }
  AbstractThread *_t = thread_;
  _t->quit();
  thread_ = nullptr;
}

void AbstractThread::Waiter::wait() {
  thread_ = AbstractThread::current();
  AbstractThread *_t = thread_;
  int _q = 0;
  for (;;) {
    _t->exec();
    if (!thread_) break;
    _q++;
  }
  while (_q--) _t->quit();
}

bool AbstractThread::Waiter::waiting() { return thread_; }

AbstractThread::Private::List::~List() {
  if (!empty()) lsError() << "thread list not empty:" << size();
  while (!empty()) {
    AbstractThread *_t = back();
    _t->quit();
    _t->waitFinished();
    delete _t;
  }
  lsDebug() << LogStream::Color::Magenta << size();
}

AbstractThread::AbstractThread(const std::string &name) : private_(*new Private) {
  private_.name = name;
  LockGuard lock(Private::list.mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(Private::list.begin(), Private::list.end(), this, Private::Compare());
  Private::list.insert(it, this);
  trace() << "threads:" << std::to_string(Private::list.size());
#ifdef POLL_WAIT
  pollfd _w;
  _w.events = POLLIN;
  private_.fds_.push_back(_w);
#endif

#ifdef PIPE_WAKE
  ::pipe(private_.pipe);
  private_.WAKE_FD = private_.pipe[0];
#elif defined EVENTFD_WAKE
  private_.WAKE_FD = eventfd(0, EFD_NONBLOCK);
#elif defined SOCKET_PAIR_WAKE
  struct sockaddr_in addr;
  private_.WAKE_FD = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  private_.WAKE_FD_WRITE = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int reuse = 1;
  ::setsockopt(private_.WAKE_FD, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  ::bind(private_.WAKE_FD, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  socklen_t _tmp = sizeof(addr);
  ::getsockname(private_.WAKE_FD, reinterpret_cast<sockaddr *>(&addr), &_tmp);
  ::connect(private_.WAKE_FD_WRITE, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
#elif defined SOCKET_CLOSE_WAKE
  private_.WAKE_FD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif

#ifdef EPOLL_WAIT
  private_.epoll_fd = epoll_create1(0);
  struct epoll_event event;
  event.events = EPOLLIN
  #ifdef EPOLL_EDGE_TRIGGERED
                 | EPOLLET
  #endif
      ;
  private_.wake_task.task = nullptr;
  event.data.ptr = &private_.wake_task;
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_ADD, private_.WAKE_FD, &event);
#elif defined IO_URING_WAIT
  if (io_uring_queue_init(IO_URING_QUEUE_SIZE, &private_.ring, 0) < 0) lsError() << "io_uring init failed";
  #ifdef EVENTFD_WAKE
  struct io_uring_sqe *sqe = io_uring_get_sqe(&private_.ring);
  if (sqe) {
    private_.wake_task.task = nullptr;
    private_.wake_task.fd = private_.WAKE_FD;
    io_uring_prep_poll_multishot(sqe, private_.WAKE_FD, POLLIN_);
    io_uring_sqe_set_data(sqe, &private_.wake_task);
    io_uring_submit(&private_.ring);
  }
  #endif
#endif
  lsTrace() << LOG_THREAD_NAME;
}

AbstractThread::~AbstractThread() {
  warning_if(std::this_thread::get_id() == private_.id) << LogStream::Color::Red << "executed from own thread" << LOG_THREAD_NAME;
  if (AbstractThread::running()) {
    lsError() << LogStream::Color::Red << "destroy running thread" << LOG_THREAD_NAME;
    quit();
    waitFinished();
  }

  {  //lock scope
    LockGuard lock(Private::list.mutex);
    std::vector<AbstractThread *>::iterator it = std::lower_bound(Private::list.begin(), Private::list.end(), this, Private::Compare());
    if (it != Private::list.end() && (*it) == this) Private::list.erase(it);
    else { lsError() << "thread not found"; }
    trace() << "threads:" << std::to_string(Private::list.size());
  }
#ifndef IO_URING_WAKE
  close_fd(private_.WAKE_FD);
  #if !defined EVENTFD_WAKE && !defined SOCKET_CLOSE_WAKE
  close_fd(private_.WAKE_FD_WRITE);
  #endif
#endif
#ifdef IO_URING_WAIT
  struct io_uring_sqe *sqe = io_uring_get_sqe(&private_.ring);
  if (sqe) {
    io_uring_prep_cancel64(sqe, 0, IORING_ASYNC_CANCEL_ANY);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    io_uring_submit(&private_.ring);
    struct io_uring_cqe *cqe;
    while (io_uring_sq_ready(&private_.ring) > 0 || io_uring_peek_cqe(&private_.ring, &cqe) == 0) { //!!! rm io_uring_sq_ready
      if (io_uring_wait_cqe(&private_.ring, &cqe) < 0) break;
      Private::PollTask *_d = reinterpret_cast<Private::PollTask *>(cqe->user_data);
      warning_if(!_d || (cqe->flags & IORING_CQE_F_MORE)) << "(!_d || !(cqe->flags & IORING_CQE_F_MORE))" << _d << cqe->res << cqe->flags;
      if (_d->fd == -1 && !(cqe->flags & IORING_CQE_F_MORE)) {
        trace() << LOG_THREAD_NAME << "delete" << _d << cqe->res;
        delete _d;
      }
      io_uring_cqe_seen(&private_.ring, cqe);
    }
  } else lsError() << "error get sqe";
  io_uring_queue_exit(&private_.ring);
#endif

  lsTrace() << LOG_THREAD_NAME << LogStream::Color::Magenta << private_.id << LogStream::Color::Default << "-" << private_.tasks.size() << private_.timers.size() << private_.poll_tasks.size() << "-" << private_.process_tasks_.size() << private_.process_timer_tasks_.size() << private_.process_poll_tasks_.size();

  if (!private_.tasks.empty()) {
    lsDebug() << LogStream::Color::DarkRed << "task list not empty" << private_.tasks.size();
    std::swap(private_.process_tasks_, private_.tasks);
    private_.process_tasks();
  }
  if (!private_.timers.empty()) {
    lsDebug() << LogStream::Color::DarkRed << "timer list not empty" << private_.timers.size();
    while (!private_.timers.empty()) {
      Private::Timer _timer = private_.timers.back();
      private_.timers.pop_back();
      trace() << _timer.id;
      delete _timer.task;
    }
  }
  if (!private_.poll_tasks.empty()) {
    lsWarning() << LogStream::Color::DarkRed << "poll task list not empty" << private_.poll_tasks.size();
    while (!private_.poll_tasks.empty()) {
      Private::PollTask *_pt = private_.poll_tasks.back();
      private_.poll_tasks.erase(private_.poll_tasks.end());
      delete _pt;
    }
  }

  delete &private_;
}

AbstractThread *AbstractThread::current() {
  std::thread::id _id = std::this_thread::get_id();
  {  //lock scope
    LockGuard lock(Private::list.mutex);
    std::vector<AbstractThread *>::iterator it = std::lower_bound(Private::list.begin(), Private::list.end(), _id, Private::Compare());
    if (it != Private::list.end() && (*it)->private_.id == _id) return (*it);
  }
  lsError() << "thread not found:" << _id;
  return nullptr;
}

AbstractThread::LockGuard AbstractThread::threads(std::vector<AbstractThread *> **list) {
  *list = &Private::list;
  return LockGuard {Private::list.mutex};
}

void AbstractThread::startedEvent() { lsDebug() << LOG_THREAD_NAME; }

void AbstractThread::finishedEvent() { lsDebug() << LOG_THREAD_NAME; }

bool AbstractThread::running() const {
  LockGuard lock(private_.mutex);
  return private_.state > Private::WaitStarted && private_.state < Private::WaitFinished;
}

void AbstractThread::requestInterrupt() {
  LockGuard lock(private_.mutex);
  if (private_.state != Private::Interrupted) {
    if (!private_.state || private_.state >= Private::WaitFinished) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "not running");
      return;
    }
    private_.state = Private::WaitInterrupted;
    private_.wake();
  }
}

bool AbstractThread::interruptRequested() const {
  LockGuard lock(private_.mutex);
  return private_.state == Private::WaitInterrupted || (private_.state >= Private::WaitFinished);
}

void AbstractThread::waitInterrupted() const {
  std::unique_lock<std::mutex> lock(private_.mutex);
  checkDifferentThread();
  if (private_.state == Private::Interrupted) return;
  if (private_.state != Private::WaitInterrupted) {
    console_msg("AbstractThread " + LOG_THREAD_NAME, "interrupt request not set");
    return;
  }
  while (private_.state == Private::WaitInterrupted) private_.condition_variable.wait(lock);
}

void AbstractThread::quit() {
  lsTrace() << LOG_THREAD_NAME;
  LockGuard lock(private_.mutex);
  if (!private_.state || (private_.state & Private::Finished)) {
    console_msg("AbstractThread " + LOG_THREAD_NAME, "thread already finished or not started");
    return;
  }
  if (private_.nested_) private_.nested_--;
  private_.state |= Private::WaitFinished;
  private_.wake();
}

void AbstractThread::waitFinished() const {
  std::unique_lock<std::mutex> lock(private_.mutex);
  checkDifferentThread();
  if (!private_.state) {
    console_msg("AbstractThread " + LOG_THREAD_NAME, "thread not running");
    return;
  }
  while (private_.state != Private::Finished) private_.condition_variable.wait(lock);
}

void AbstractThread::setId() {
  std::thread::id _id = std::this_thread::get_id();
  std::vector<AbstractThread *>::iterator it = std::lower_bound(Private::list.begin(), Private::list.end(), this, Private::Compare());
  if (it != Private::list.end() && (*it) == this) {
    Private::list.erase(it);
    private_.id = _id;
    it = std::lower_bound(Private::list.begin(), Private::list.end(), this, Private::Compare());
    Private::list.insert(it, this);
    trace() << LOG_THREAD_NAME << LogStream::Color::Magenta << _id;
    return;
  }
  lsError() << "thread not found:" << _id;
}

void AbstractThread::clearId() {
  std::thread::id _id = std::this_thread::get_id();
  std::vector<AbstractThread *>::iterator it = std::lower_bound(Private::list.begin(), Private::list.end(), _id, Private::Compare());
  if (it != Private::list.end() && (*it) == this && private_.id == _id) {
    AbstractThread *_t = (*it);
    _t->private_.id = {};
    Private::list.erase(it);
    it = std::lower_bound(Private::list.begin(), Private::list.end(), _t, Private::Compare());
    Private::list.insert(it, _t);
    trace() << '(' + _t->private_.name + ')' << LogStream::Color::Magenta << _id;
    return;
  }
  lsError() << "thread not found:" << LogStream::Color::Magenta << _id;
}

void AbstractThread::exec() {
  int _nested;
  {  //lock scope
    LockGuard lock(private_.mutex);
    warning_if(private_.process_tasks_.size() || private_.process_poll_tasks_.size() || private_.process_timer_tasks_.size()) << LogStream::Color::Red << "not empty" << private_.process_tasks_.size() << private_.process_poll_tasks_.size() << private_.process_timer_tasks_.size();
    if (private_.state >= Private::Running && private_.state < Private::Finished) { private_.nested_++; }
    _nested = private_.nested_;

    if (_nested) {  //nested exec
      trace() << LogStream::Color::Red << "nested" << LogStream::Color::Green << _nested << LOG_THREAD_NAME << private_.process_tasks_.size() << private_.process_poll_tasks_.size() << private_.process_timer_tasks_.size();
      if (!private_.process_tasks_.empty()) {
        private_.mutex.unlock();
        private_.process_tasks();
        private_.mutex.lock();
      } else if (!private_.process_poll_tasks_.empty()) {
        private_.mutex.unlock();
        private_.process_polls();
        private_.mutex.lock();
      } else if (!private_.process_timer_tasks_.empty()) {
        private_.mutex.unlock();
        private_.process_timers();
        private_.mutex.lock();
      }
    }
    if (!_nested) {
      private_.state = Private::Running;
      private_.mutex.unlock();
      startedEvent();
      private_.mutex.lock();
    }
    std::swap(private_.process_tasks_, private_.tasks);  //take exists tasks
  }
  for (;;) {
    private_.process_tasks();
    {  //lock scope, wait new tasks or wakeup
      std::unique_lock<std::mutex> lock(private_.mutex);
    CONTINUE:
      if (!private_.tasks.empty()) {
        std::swap(private_.process_tasks_, private_.tasks);  //take new tasks
        if (!private_.wake_ || private_.process_tasks_.size() <= QUEUE_TASKS_OVERLOAD_SIZE) continue;
        //invoke tasks and check poll descriptors and timers
        console_msg("AbstractThread " + LOG_THREAD_NAME, "queue tasks overload, warning limit: " + std::to_string(QUEUE_TASKS_OVERLOAD_SIZE) + ", size: " + std::to_string(private_.process_tasks_.size()));
        private_.mutex.unlock();
        private_.process_tasks();
        private_.mutex.lock();
      }
      if (private_.state & Private::WaitFinished) break;
      if (private_.state == Private::WaitInterrupted) {
        private_.state = Private::Interrupted;
        private_.condition_variable.notify_all();
      }

      std::chrono::time_point now = std::chrono::steady_clock::now();

      if (private_.wakeup > now) {
        private_.wake_ = false;
        if (private_.poll_tasks.empty()) {
          if (private_.condition_variable.wait_until(lock, private_.wakeup) == std::cv_status::no_timeout) {
            private_.wake_ = true;
            goto CONTINUE;
          }
        } else {
          uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(private_.wakeup - now).count();
#ifdef POLL_WAIT
          if (!private_.update_pollfd.empty()) {
            for (const struct Private::update_pollfd &pfd : private_.update_pollfd) {
              std::vector<pollfd>::iterator it = std::lower_bound(private_.fds_.begin() + 1, private_.fds_.end(), pfd.fd, Private::Compare());
              if (pfd.action == 0) {
                it->events = pfd.events;
                continue;
              }
              if (pfd.action == 1) {
                private_.fdts_.insert(private_.fdts_.begin() + (it - 1 - private_.fds_.begin()), pfd.task);
                pollfd _pfd;
                _pfd.fd = pfd.fd;
                _pfd.events = pfd.events;
                private_.fds_.insert(it, _pfd);
                continue;
              }
              if (pfd.action == -1) {
                private_.fdts_.erase(private_.fdts_.begin() + (it - 1 - private_.fds_.begin()));
                private_.fds_.erase(it);
                continue;
              }
            }
            private_.update_pollfd.clear();
          }

          private_.mutex.unlock();
          int r = poll(private_.fds_.data(), private_.fds_.size(), ms);
          private_.mutex.lock();

          int i = 0;
          if (private_.wake_) {
            trace() << LogStream::Color::Magenta << "waked" << LOG_THREAD_NAME << private_.fds_[0].revents << r << private_.WAKE_FD;
  #ifdef EVENTFD_WAKE
            eventfd_t _v;
            eventfd_read(private_.WAKE_FD, &_v);
  #elif defined SOCKET_CLOSE_WAKE
            private_.WAKE_FD = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  #else
            char _c;
    #ifndef __clang_analyzer__
            read_fd(private_.WAKE_FD, &_c, sizeof(_c));
    #endif
  #endif
            if (private_.fds_[0].revents) {
              if (r == 1) goto CONTINUE;
              i = 1;
            }
          };

          if (r > 0) {
            for (std::vector<pollfd>::const_iterator it = private_.fds_.begin() + 1; i != r; ++it)
              if (it->revents) {
                private_.process_poll_tasks_.push({static_cast<int>(it->fd), it->revents, *(private_.fdts_.begin() + (it - 1 - private_.fds_.begin()))});
                i++;
              }
#elif defined EPOLL_WAIT
          struct epoll_event event[EPOLL_WAIT_EVENTS];
          private_.mutex.unlock();
          int r = epoll_wait(private_.epoll_fd, event, EPOLL_WAIT_EVENTS, ms);
          private_.mutex.lock();
          if (r > 0) {
            for (int i = 0; i != r; ++i) {
              if (event[i].data.ptr == &private_.wake_task) {
                trace() << LogStream::Color::Magenta << "waked" << LOG_THREAD_NAME << event[i].events << r << private_.WAKE_FD << private_.wake_;
  #ifdef EVENTFD_WAKE
                eventfd_t _v;
                eventfd_read(private_.WAKE_FD, &_v);
  #elif defined SOCKET_CLOSE_WAKE
                if (!private_.wake_) {
                  trace() << LogStream::Color::Red << event[i].events;
                  continue;
                }
                close_fd(private_.WAKE_FD);
                private_.WAKE_FD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                event[i].events = 0;
                epoll_ctl(private_.epoll_fd, EPOLL_CTL_ADD, private_.WAKE_FD, &event[i]);
  #else
                char _c;
    #ifndef __clang_analyzer__
                read_fd(private_.WAKE_FD, &_c, sizeof(_c));
    #endif
  #endif
                continue;
              }
              Private::PollTask *_d = static_cast<Private::PollTask *>(event[i].data.ptr);
              trace() << "append poll task" << _d << _d->fd << event[i].events;
              private_.process_poll_tasks_.push_back({_d->fd, event[i].events, _d->task});
            }
#elif defined IO_URING_WAIT
          struct io_uring_cqe *cqe = nullptr;
          struct __kernel_timespec ts;
          ts.tv_sec = ms / 1000;
          ts.tv_nsec = (ms % 1000) * 1000000LL;

          private_.mutex.unlock();
          int r = io_uring_wait_cqe_timeout(&private_.ring, &cqe, &ts);
          private_.mutex.lock();

          if (r == 0) {
            uint32_t head;
            io_uring_for_each_cqe(&private_.ring, head, cqe) {
              r++;
  #ifdef IO_URING_WAKE
              if (cqe->user_data == static_cast<uint64_t>(-1)) {
                trace() << LogStream::Color::Magenta << "waked" << LOG_THREAD_NAME << private_.wake_;
  #elif defined EVENTFD_WAKE
              if ((Private::PollTask *)cqe->user_data == &private_.wake_task) {
                trace() << LogStream::Color::Magenta << "waked" << LOG_THREAD_NAME << private_.WAKE_FD << private_.wake_;
                eventfd_t _v;
                eventfd_read(private_.WAKE_FD, &_v);
  #endif
              } else {
                Private::PollTask *_d = reinterpret_cast<Private::PollTask *>(cqe->user_data);
                trace() << "cqe" << cqe->res << cqe->flags << _d;
                if (!cqe->res || !_d) continue;

                if (cqe->res < 0) {  //!!!
                  if (_d->fd == -1) {
                    _d->fd = -2;
                    trace() << LogStream::Color::Yellow << "append delete task" << _d << _d->fd;
                    private_.tasks.push(new Invocable<void()>::Function([p = _d] { delete p; }));
                  }
                  continue;
                }

                if (_d->fd == -1) {
                  trace() << LogStream::Color::DarkRed << "(_d->fd == -1)";
                  continue;
                }

                int32_t events = cqe->res & (POLLIN_ | POLLOUT_ | POLLERR_ | POLLHUP_ | POLLNVAL_);
                if (events) {
                  std::deque<Private::ProcessPollTask>::iterator it = std::lower_bound(private_.process_poll_tasks_.begin(), private_.process_poll_tasks_.end(), _d->fd, Private::Compare());
                  if (it == private_.process_poll_tasks_.end() || it->fd != _d->fd) {
                    trace() << "append poll task" << LogStream::Color::Gray << _d << _d->fd << events;
                    private_.process_poll_tasks_.insert(it, {_d->fd, events, _d->task});
                  } else {
                    trace() << "update poll task" << _d << _d->fd << events;
                    it->events |= events;
                  }
                }
              }
            }
            if (r > 0) { io_uring_cq_advance(&private_.ring, r); }
#endif
            if (r > 0) {  //!!!
              if (private_.process_poll_tasks_.empty()) goto CONTINUE;
              private_.wake_ = true;
              private_.mutex.unlock();
              private_.process_polls();
              private_.mutex.lock();
            }
            goto CONTINUE;
          }
        }
      }

      private_.wakeup = std::chrono::time_point<std::chrono::steady_clock>::max();
      if (!private_.timers.empty()) {
        for (Private::Timer &t : private_.timers) {
          if (t.timeout == std::chrono::milliseconds(0)) continue;
          if (t.expire <= now) {
            t.expire += t.timeout;
            if (t.expire <= now) {
              console_msg("AbstractThread " + LOG_THREAD_NAME, "timer overload, interval: " + std::to_string(t.timeout.count()) + ", expired: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>((now - t.expire + t.timeout)).count()));
              t.expire = now + t.timeout;
            }
            private_.process_timer_tasks_.push({t.id, t.task});
          }
          if (private_.wakeup > t.expire) private_.wakeup = t.expire;
        }
        if (private_.process_timer_tasks_.empty()) goto CONTINUE;
        private_.wake_ = true;
        private_.mutex.unlock();
        private_.process_timers();
        private_.mutex.lock();
        goto CONTINUE;
      }
    }
  }
  LockGuard lock(private_.mutex);
  if (_nested--) {
    lsTrace() << LOG_THREAD_NAME << LogStream::Color::Magenta << "end" << _nested << private_.nested_ << LogStream::Color::Red << (_nested <= private_.nested_);
    if (_nested <= private_.nested_) private_.state &= ~Private::WaitFinished;
    return;
  }
  private_.mutex.unlock();
  finishedEvent();
  private_.mutex.lock();
  private_.state = Private::WaitFinished | Private::Finished;
  std::swap(private_.process_tasks_, private_.tasks);
  private_.mutex.unlock();
  private_.process_tasks();
  private_.mutex.lock();

#ifdef IO_URING_WAIT
  struct io_uring_cqe *cqe;
  while (io_uring_peek_cqe(&private_.ring, &cqe) == 0) {
    trace() << "io_uring_peek_cqe" << cqe;
    if (!cqe) break;
    Private::PollTask *_d = reinterpret_cast<Private::PollTask *>(cqe->user_data);
    warning_if(!_d || (cqe->flags & IORING_CQE_F_MORE)) << "(!_d || !(cqe->flags & IORING_CQE_F_MORE))" << _d << cqe->res << cqe->flags;
    if (_d->fd == -1 && !(cqe->flags & IORING_CQE_F_MORE)) {
      trace() << LOG_THREAD_NAME << "delete" << _d << cqe->res;
      delete _d;
    }
    io_uring_cqe_seen(&private_.ring, cqe);
  }
#endif
}

void AbstractThread::processTasks() const {
  checkCurrentThread();
  private_.process_tasks();
  {  //lock scope
    LockGuard lock(private_.mutex);
    std::swap(private_.process_tasks_, private_.tasks);
  }
  private_.process_tasks();
}

void AbstractThread::Private::wake() {
  if (wake_) return;
  warning_if(std::this_thread::get_id() == id) << LogStream::Color::Red << '(' + name + ')';
  wake_ = true;
  trace() << LogStream::Color::Cyan << poll_tasks.empty();
  if (poll_tasks.empty()) {
    condition_variable.notify_all();
    return;
  }
#ifdef IO_URING_WAKE
  struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  if (sqe) {
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(-1));
    io_uring_submit(&ring);
  }
#elif defined EVENTFD_WAKE
  eventfd_write(WAKE_FD_WRITE, 1);
#elif defined SOCKET_CLOSE_WAKE
  #ifdef POLL_WAIT
  close_fd(WAKE_FD_WRITE);
  #elif defined EPOLL_WAIT
  ::shutdown(WAKE_FD_WRITE, SHUT_RDWR);
  #endif
#else
  char _c = '\x0';
  write_fd(WAKE_FD_WRITE, &_c, 1);
#endif
}

bool AbstractThread::invokeTask(AbstractTask *task) const {
  {  //lock scope
    LockGuard lock(private_.mutex);
    warning_if(!private_.state) << LogStream::Color::Red << "thread not running" << LOG_THREAD_NAME << private_.id;
    if (private_.state < Private::Finished) {
      if (private_.state == Private::Interrupted) private_.state = Private::Running;
      private_.tasks.push(task);
      trace() << LogStream::Color::Gray << LOG_THREAD_NAME << "invoke tasks" << private_.tasks.size();
      private_.wake();
      return true;
    }
  }
  lsTrace() << "thread" << LOG_THREAD_NAME << "finished";
  return false;
}

void AbstractThread::start() {
  std::unique_lock<std::mutex> lock(private_.mutex);
  if ((private_.state && private_.state != Private::Finished) || private_.id != std::thread::id()) {
    console_msg("AbstractThread " + LOG_THREAD_NAME, "thread already running or starting, or thread id not null");
    return;
  }
  private_.state = Private::WaitStarted;
  std::thread t {[this]() {
    {  //lock scope
      LockGuard lock_list(Private::list.mutex);
      LockGuard lock(private_.mutex);
      setId();
      private_.condition_variable.notify_all();
    }
    exec();
    {  //lock scope
      LockGuard lock_list(Private::list.mutex);
      LockGuard lock(private_.mutex);
      clearId();
      private_.state = Private::Finished;
      private_.condition_variable.notify_all();
    }
  }};
  t.detach();
  private_.condition_variable.wait(lock);
}

int AbstractThread::workLoad() const {
  LockGuard lock(private_.mutex);
  int n = private_.tasks.size();
  if (private_.wake_) ++n;
  trace() << n;
  return n;
}

std::thread::id AbstractThread::id() const { return private_.id; }

std::string AbstractThread::name() const { return private_.name; }

AbstractThread::LockGuard AbstractThread::lockGuard() const { return LockGuard(private_.mutex); }

int AbstractThread::appendTimer(int ms, AbstractTask *task) {
  int id = 0;
  LockGuard lock(private_.mutex);
  std::vector<Private::Timer>::iterator it = private_.timers.begin();
  for (; it != private_.timers.end(); ++it, ++id)
    if (it->id != id) break;
  std::chrono::milliseconds _ms = std::chrono::milliseconds(ms);
  std::chrono::time_point<std::chrono::steady_clock> _wakeup;
  if (_ms > std::chrono::milliseconds(0)) {
    _wakeup = std::chrono::steady_clock::now() + _ms;
    if (private_.wakeup > _wakeup) {
      private_.wakeup = _wakeup;
      private_.wake();
    }
  }
  private_.timers.emplace(it, Private::Timer {id, _ms, _wakeup, task});
  trace() << LogStream::Color::Gray << id << ms;
  return id;
}

bool AbstractThread::modifyTimer(int id, int ms) {
  LockGuard lock(private_.mutex);
  std::vector<Private::Timer>::iterator it = std::lower_bound(private_.timers.begin(), private_.timers.end(), id, Private::Compare());
  if (it != private_.timers.end() && it->id == id) {
    it->timeout = std::chrono::milliseconds(ms);
    if (it->timeout > std::chrono::milliseconds(0)) {
      it->expire = std::chrono::steady_clock::now() + it->timeout;
      if (private_.wakeup > it->expire) {
        private_.wakeup = it->expire;
        private_.wake();
      }
    }
    trace() << LogStream::Color::DarkYellow << id << ms;
    return true;
  }
  console_msg("AbstractThread " + LOG_THREAD_NAME, "timer: " + std::to_string(id) + " not found");
  return false;
}

void AbstractThread::removeTimer(int id) {
  std::vector<Private::Timer>::iterator it;
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(private_.mutex);
    it = std::lower_bound(private_.timers.begin(), private_.timers.end(), id, Private::Compare());
    if (it == private_.timers.end() || it->id != id) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "timer: " + std::to_string(id) + " not found");
      return;
    }
    trace() << LogStream::DarkYellow << LOG_THREAD_NAME << "append delete task" << id << it->task;
    _t = new Invocable<void()>::Function([p = it->task] {
      trace() << LogStream::DarkYellow << "delete" << p;
      delete p;
    });
    private_.timers.erase(it);
  }
  if (!invokeTask(_t)) {
    (*_t)();
    delete _t;
  }
  trace() << LogStream::Color::Green << LOG_THREAD_NAME << id;
}

bool AbstractThread::appendPollDescriptor(int fd, PollEvents events, AbstractPollTask *task) {
#ifdef POLL_WAIT
  LockGuard lock(private_.mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd == fd) {
    delete task;
    console_msg("AbstractThread " + LOG_THREAD_NAME, "append poll descriptor: " + std::to_string(fd) + " already exists");
    return false;
  }
  struct pollfd pollfd;
  pollfd.fd = fd;
  pollfd.events = events;
  Private::PollTask *_d = new Private::PollTask(pollfd.fd, task);
  private_.update_pollfd.push_back({fd, pollfd.events, task, 1});
  private_.wake();
  private_.poll_tasks.insert(it, _d);
#elif defined EPOLL_WAIT
  struct epoll_event event;
  event.events = events
  #ifdef EPOLL_EDGE_TRIGGERED
                 | static_cast<uint32_t>(EPOLLET)
  #endif
      ;
  Private::PollTask *_d = new Private::PollTask(fd, task);
  event.data.ptr = _d;
  if (epoll_ctl(private_.epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
  ERR:
    delete _d;
    console_msg("AbstractThread " + LOG_THREAD_NAME, "append poll descriptor: " + std::to_string(fd) + " already exists");
    return false;
  }
  LockGuard lock(private_.mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd == fd) goto ERR;
  if (private_.poll_tasks.empty()) private_.wake();
  private_.poll_tasks.insert(it, _d);
#elif defined IO_URING_WAIT
  Private::PollTask *_d = new Private::PollTask(fd, task);
  LockGuard lock(private_.mutex);
  {  //lock scope
    struct io_uring_sqe *sqe = io_uring_get_sqe(&private_.ring);
    if (!sqe) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "append poll descriptor: " + std::to_string(fd) + "  error get sqe");
    ERR:
      delete _d;
      return false;
    }
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it != private_.poll_tasks.end() && (*it)->fd == fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "append poll descriptor: " + std::to_string(fd) + " already exists");
      goto ERR;
    }
    if (private_.poll_tasks.empty()) private_.wake();
    private_.poll_tasks.insert(it, _d);
    io_uring_prep_poll_multishot(sqe, fd, events);
    io_uring_sqe_set_data(sqe, _d);
    io_uring_submit(&private_.ring);
  }
#endif
  trace() << _d << fd << static_cast<int>(events);
  return true;
}

bool AbstractThread::modifyPollDescriptor(int fd, PollEvents events) {
#ifdef POLL_WAIT
  LockGuard lock(private_.mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd != fd) {
    console_msg("AbstractThread " + LOG_THREAD_NAME, "modify poll descriptor: " + std::to_string(fd) + " not found");
    return false;
  }
  struct Private::update_pollfd v;
  v.fd = fd;
  v.events = events;
  v.action = 0;
  private_.update_pollfd.push_back(v);
  private_.wake();
#elif defined EPOLL_WAIT
  struct epoll_event event;
  event.events = events
  #ifdef EPOLL_EDGE_TRIGGERED
                 | static_cast<uint32_t>(EPOLLET)
  #endif
      ;
  {  //lock scope
    LockGuard lock(private_.mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "modify poll descriptor: " + std::to_string(fd) + " not found");
      return false;
    }
    event.data.ptr = *it;
  }
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_MOD, fd, &event);
#elif defined IO_URING_WAIT
  Private::PollTask *_d = nullptr;
  {
    LockGuard lock(private_.mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "modify poll descriptor: " + std::to_string(fd) + " not found");
      return false;
    }
    _d = *it;

    struct io_uring_sqe *sqe;
    if (!(sqe = io_uring_get_sqe(&private_.ring))) {
      lsError() << "error get sqe";
      return false;
    }
    io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(_d), 0);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    if (!(sqe = io_uring_get_sqe(&private_.ring))) {
      lsError() << "error get sqe";
      return false;
    }
    io_uring_prep_poll_multishot(sqe, fd, events);
    io_uring_sqe_set_data(sqe, _d);
    io_uring_submit(&private_.ring);
  }
#endif
  trace() << fd << static_cast<int>(events);
  return true;
}

void AbstractThread::removePollDescriptor(int fd) {
#ifdef POLL_WAIT
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(private_.mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "remove poll descriptor: " + std::to_string(fd) + " not found");
      return;
    }
    trace() << LogStream::Yellow << "append delete task" << *it << fd;
    _t = new Invocable<void()>::Function([p = *it] {
      trace() << LogStream::Yellow << "delete" << p << p->fd;
      delete p;
    });
    struct Private::update_pollfd v;
    v.fd = fd;
    v.action = -1;
    private_.update_pollfd.push_back(v);
    private_.wake();
    private_.poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    (*_t)();
    delete _t;
  }
#elif defined EPOLL_WAIT
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(private_.mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "remove poll descriptor: " + std::to_string(fd) + " not found");
      return;
    }
    trace() << LogStream::Yellow << "append delete task" << *it << fd;
    _t = new Invocable<void()>::Function([p = *it] {
      trace() << LogStream::Yellow << "delete" << p << p->fd;
      delete p;
    });
    if (private_.poll_tasks.size() == 1) private_.wake();
    private_.poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    (*_t)();
    delete _t;
  }
#elif defined IO_URING_WAIT
  Private::PollTask *_d = nullptr;
  {
    LockGuard lock(private_.mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      console_msg("AbstractThread " + LOG_THREAD_NAME, "remove poll descriptor: " + std::to_string(fd) + " not found");
      return;
    }
    _d = *it;
    trace() << LogStream::Gray << _d << fd;
    _d->fd = -1;
    if (private_.poll_tasks.size() == 1) private_.wake();
    private_.poll_tasks.erase(it);

    struct io_uring_sqe *sqe;
    if (!(sqe = io_uring_get_sqe(&private_.ring))) {
      lsError() << "error get sqe";
      return;
    }

    io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(_d), 0);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    io_uring_submit(&private_.ring);
  }
#endif
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const AbstractThread &t) {
  AbstractThread::LockGuard lock = t.lockGuard();
  return (log << '(' + t.private_.name + ')' << t.private_.id << t.private_.state << '-' << t.private_.tasks.size() << t.private_.timers.size() << t.private_.poll_tasks.size());
}
}  // namespace AsyncFw
