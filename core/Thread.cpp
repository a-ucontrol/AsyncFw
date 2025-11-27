#include <queue>
#include <signal.h>
#include "LogStream.h"
#include "Thread.h"
#include "Socket.h"

#define EVENTFD_WAKE
#define EPOLL_WAIT

#ifdef EXTEND_THREAD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

#ifndef _WIN32
  #ifdef EVENTFD_WAKE
    #include <sys/eventfd.h>
  #endif
  #ifdef EPOLL_WAIT
    #include <sys/epoll.h>
    #define EPOLL_WAIT_EVENTS 32
  #else
    #include <sys/poll.h>
  #endif
#else
  #undef EVENTFD_WAKE
  #undef EPOLL_WAIT
  #include <winsock2.h>
  #include <fcntl.h>
  #define poll(ptr, size, timeout) WSAPoll(ptr, size, timeout)
#endif

#ifdef EPOLL_WAIT
  #define WAKE_FD private_.wake_task.fd
#else
  #define WAKE_FD private_.fds_[0].fd
#endif

#include "console_msg.hpp"

using namespace AsyncFw;
AbstractTask::~AbstractTask() {
#if 0
  console_msg(__PRETTY_FUNCTION__);
#endif
}

struct AbstractThread::Private {
  struct Timer {
    int id;
    std::chrono::milliseconds timeout;
    std::chrono::time_point<std::chrono::steady_clock> expire;
    AbstractTask *task = nullptr;
  };

  struct PollTask {
    ~PollTask() {
      if (task) delete task;
    }
    int fd;
    AbstractTask *task;
  };

  struct Compare {
    bool operator()(const Timer &t, int id) const { return t.id < id; }
    bool operator()(const PollTask *d, int fd) const { return d->fd < fd; }
#ifndef EPOLL_WAIT
    bool operator()(const pollfd pfd, int fd) const { return pfd.fd < fd; }
#endif
  };

  std::queue<AbstractTask *> tasks;
  std::vector<Timer> timers;
  std::chrono::time_point<std::chrono::steady_clock> wakeup = std::chrono::time_point<std::chrono::steady_clock>::max();
  bool wake_ = true;

  std::vector<PollTask *> poll_tasks;

#if !defined EVENTFD_WAKE && !defined _WIN32
  int pipe[2];
#endif

#ifndef EPOLL_WAIT
  struct update_pollfd {
    int fd;
    short int events;
    AbstractTask *task;
    int8_t action;
  };
  std::vector<struct update_pollfd> update_pollfd;
  std::vector<pollfd> fds_;
  std::vector<AbstractTask *> fdts_;
#else
  int epoll_fd;
  PollTask wake_task;
#endif

  struct ProcessTimerTask {
    int id;
    AbstractTask *task;
  };
  struct ProcessPollTask {
    int fd;
#ifndef EPOLL_WAIT
    short int events;
#else
    uint32_t events;
#endif
    AbstractTask *task;
  };

  void process_tasks();
  void process_polls();
  void process_timers();
  int state = None;
  std::thread::id id;
  std::string name;
  std::queue<AbstractTask *> process_tasks_;
  std::queue<ProcessTimerTask> process_timer_tasks_;
  std::queue<ProcessPollTask> process_poll_tasks_;
};

void AbstractThread::Private::process_tasks() {
  for (; !process_tasks_.empty();) {
    AbstractTask *_t = process_tasks_.front();
    process_tasks_.pop();
    _t->invoke();
    delete _t;
  }
}

void AbstractThread::Private::process_polls() {
  for (; !process_poll_tasks_.empty();) {
    Private::ProcessPollTask _pt = process_poll_tasks_.front();
    process_poll_tasks_.pop();
    static_cast<InternalPoolTask<AbstractThread::PollEvents> *>(_pt.task)->e_ = static_cast<AbstractThread::PollEvents>(_pt.events);
    _pt.task->invoke();
  }
}

void AbstractThread::Private::process_timers() {
  for (; !process_timer_tasks_.empty();) {
    Private::ProcessTimerTask _tt = process_timer_tasks_.front();
    process_timer_tasks_.pop();
    _tt.task->invoke();
  }
}

void AbstractThread::Holder::complete() {
  waiting = false;
  thread->quit();
}

void AbstractThread::Holder::wait() {
  waiting = true;
  thread = AbstractThread::currentThread();
  bool _q = false;
  for (;;) {
    thread->exec();
    if (!waiting) {
      if (thread->private_.state == AbstractThread::Finished) thread->private_.state = AbstractThread::None;
      break;
    }
    _q = true;
  }
  if (_q) thread->quit();
}

bool AbstractThread::Compare::operator()(const AbstractThread *t, std::thread::id id) const { return t->private_.id < id; }
bool AbstractThread::Compare::operator()(const AbstractThread *t1, const AbstractThread *t2) const {
  if (t1->private_.id != t2->private_.id) return t1->private_.id < t2->private_.id;
  return t1 < t2;
}

AbstractThread::AbstractThread(const std::string &_name) : private_(*new Private) {
  private_.name = _name;
  LockGuard lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(list_threads.begin(), list_threads.end(), this, Compare());
  list_threads.insert(it, this);
  trace() << "threads:" << std::to_string(list_threads.size());

#ifndef EPOLL_WAIT
  pollfd _w;
  _w.events = POLLIN;
  private_.fds_.push_back(_w);
  #ifndef _WIN32
    #ifndef EVENTFD_WAKE
  if (::pipe(private_.pipe)) console_msg("AbstractThread: error create pipe");
  WAKE_FD = private_.pipe[0];
    #else
  WAKE_FD = eventfd(0, EFD_NONBLOCK);
    #endif
  #else
  WAKE_FD = socket(AF_INET, 0, 0);
  #endif
#else
  private_.epoll_fd = epoll_create1(0);
  struct epoll_event event;
  event.events = EPOLLIN;
  #ifdef EVENTFD_WAKE
  private_.wake_task.fd = eventfd(0, EFD_NONBLOCK);
  #else
  if (::pipe(private_.pipe)) console_msg("AbstractThread: error create pipe");
  private_.wake_task.fd = private_.pipe[0];
  #endif
  private_.wake_task.task = nullptr;
  event.data.ptr = &private_.wake_task;
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_ADD, WAKE_FD, &event);
#endif

  lsTrace() << _name;
}

AbstractThread::~AbstractThread() {
#ifndef _WIN32
  ::close(WAKE_FD);
  #ifndef EVENTFD_WAKE
  ::close(private_.pipe[1]);
  #endif
  #ifdef EPOLL_WAIT
  ::close(private_.epoll_fd);
  #endif
#else
  ::closesocket(WAKE_FD);
#endif

  warning_if(std::this_thread::get_id() == private_.id) << LogStream::Color::Red << "executed from own thread (" + private_.name + ")";
  if (running()) {
    lsWarning() << "destroy running thread";
    quit();
    waitFinished();
  }

  lsTrace() << private_.name << "-" << private_.tasks.size() << private_.timers.size() << private_.poll_tasks.size() << private_.process_tasks_.size() << private_.process_timer_tasks_.size() << private_.process_poll_tasks_.size();

  if (!private_.tasks.empty()) {
    lsDebug() << LogStream::Color::Red << "task list not empty";
    std::swap(private_.process_tasks_, private_.tasks);
    private_.process_tasks();
  }

  delete &private_;

  LockGuard lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(list_threads.begin(), list_threads.end(), this, Compare());
  if (it != list_threads.end() && (*it) == this) list_threads.erase(it);
  else { lsError() << "thread not found"; }
  trace() << "threads:" << std::to_string(list_threads.size());
}

AbstractThread *AbstractThread::currentThread() {
  std::thread::id _id = std::this_thread::get_id();
  {  //lock scope
    LockGuard lock(list_mutex);
    std::vector<AbstractThread *>::iterator it = std::lower_bound(list_threads.begin(), list_threads.end(), _id, Compare());
    if (it != list_threads.end() && (*it)->private_.id == _id) return (*it);
  }
  lsError() << "thread not found:" << _id;
  return nullptr;
}

bool AbstractThread::running() {
  LockGuard lock(mutex);
  return private_.state > WaitStarted && private_.state != Finished;
}

void AbstractThread::requestInterrupt() {
  LockGuard lock(mutex);
  if (private_.state != Interrupted) {
    private_.state = WaitInterrupted;
    wake();
  }
}

bool AbstractThread::interruptRequested() const {
  LockGuard lock(mutex);
  return private_.state == WaitInterrupted || private_.state == WaitFinished;
}

void AbstractThread::waitInterrupted() const {
  checkDifferentThread();
  std::unique_lock<std::mutex> lock(mutex);
  if (private_.state == Interrupted) return;
  if (private_.state != WaitInterrupted) {
    lsWarning("interrupt request not set");
    return;
  }
  while (private_.state == WaitInterrupted) condition_variable.wait(lock);
}

void AbstractThread::quit() {
  lsDebug() << name() << this;
  LockGuard lock(mutex);
  if (!private_.state || private_.state == Finished) {
    lsWarning() << "thread already finished or not started";
    return;
  }
  private_.state = WaitFinished;
  wake();
}

void AbstractThread::waitFinished() const {
  checkDifferentThread();
  std::unique_lock<std::mutex> lock(mutex);
  if (!private_.state) {
    lsWarning() << "thread not running";
    return;
  }
  while (private_.state != Finished) condition_variable.wait(lock);
}

void AbstractThread::changeId(std::thread::id _id) {
  LockGuard lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(list_threads.begin(), list_threads.end(), this, Compare());
  if (it != list_threads.end() && (*it) == this) list_threads.erase(it);
  else { lsError() << "thread not found"; }
  private_.id = _id;
  it = std::lower_bound(list_threads.begin(), list_threads.end(), this, Compare());
  list_threads.insert(it, this);
}

void AbstractThread::exec() {
  int _state;

  {  //lock scope
    LockGuard lock(mutex);
    warning_if(private_.process_tasks_.size() || private_.process_poll_tasks_.size() || private_.process_timer_tasks_.size()) << LogStream::Color::Red << "not empty" << private_.process_tasks_.size() << private_.process_poll_tasks_.size() << private_.process_timer_tasks_.size();
    _state = (private_.state != Finished) ? private_.state : None;
    if (_state >= Running) {  //nested exec
      trace() << LogStream::Color::Red << "nested" << private_.process_tasks_.size() << private_.process_poll_tasks_.size() << private_.process_timer_tasks_.size();
      if (!private_.process_tasks_.empty()) {
        mutex.unlock();
        private_.process_tasks();
        mutex.lock();
      } else if (!private_.process_poll_tasks_.empty()) {
        mutex.unlock();
        private_.process_polls();
        mutex.lock();
      } else if (!private_.process_timer_tasks_.empty()) {
        mutex.unlock();
        private_.process_timers();
        mutex.lock();
      }
    }

    std::swap(private_.process_tasks_, private_.tasks);  //take exists tasks
    private_.state = Running;
    if (_state < Running) condition_variable.notify_all();
  }
  if (_state < Running) { startedEvent(); }
  for (;;) {
    private_.process_tasks();
    {  //lock scope, wait new tasks or wakeup
      std::unique_lock<std::mutex> lock(mutex);
    CONTINUE:
      if (!private_.tasks.empty()) {
        std::swap(private_.process_tasks_, private_.tasks);  //take new tasks
        continue;
      }
      if (private_.state == WaitFinished) break;
      if (private_.state == WaitInterrupted) private_.state = Interrupted;

      condition_variable.notify_all();
      std::chrono::time_point now = std::chrono::steady_clock::now();

      if (private_.wakeup > now) {
        private_.wake_ = false;
        if (private_.poll_tasks.empty()) {
          if (condition_variable.wait_until(lock, private_.wakeup) == std::cv_status::no_timeout) {
            private_.wake_ = true;
            goto CONTINUE;
          }
        } else {
          uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(private_.wakeup - now).count();
#ifndef EPOLL_WAIT
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

          mutex.unlock();
          int r = poll(private_.fds_.data(), private_.fds_.size(), ms);
          mutex.lock();

          int i = 0;
          if (private_.wake_) {
            trace() << LogStream::Color::Magenta << "waked" << private_.name << private_.id << private_.fds_[0].revents << r;
  #ifndef _WIN32
    #ifndef EVENTFD_WAKE
            char _c;
      #ifndef __clang_analyzer__
            (void)read(WAKE_FD, &_c, sizeof(_c));
      #endif
    #else
            eventfd_t _v;
            (void)eventfd_read(WAKE_FD, &_v);
    #endif
  #else
            WAKE_FD = socket(AF_INET, 0, 0);
  #endif
            if (private_.fds_[0].revents) {
              if (r == 1) goto CONTINUE;
              i = 1;
            }
          }

          if (r > 0) {
            for (std::vector<pollfd>::const_iterator it = private_.fds_.begin() + 1; i != r; ++it)
              if (it->revents) {
                private_.process_poll_tasks_.push({it->fd, it->revents, *(private_.fdts_.begin() + (it - 1 - private_.fds_.begin()))});
                i++;
              }
#else
          struct epoll_event event[EPOLL_WAIT_EVENTS];
          mutex.unlock();
          int r = epoll_wait(private_.epoll_fd, event, EPOLL_WAIT_EVENTS, ms);
          mutex.lock();
          if (r > 0) {
            for (int i = 0; i != r; ++i) {
              if (static_cast<Private::PollTask *>(event[i].data.ptr)->fd == WAKE_FD) {
  #ifndef EVENTFD_WAKE
                char _c;
    #ifndef __clang_analyzer__
                (void)read(WAKE_FD, &_c, sizeof(_c));
    #endif
  #else
                eventfd_t _v;
                (void)eventfd_read(WAKE_FD, &_v);
  #endif
                trace() << LogStream::Color::Magenta << "waked" << private_.name << private_.id << event[i].events << private_.wake_;
                continue;
              }
              Private::PollTask *_d = static_cast<Private::PollTask *>(event[i].data.ptr);
              private_.process_poll_tasks_.push({_d->fd, event[i].events, _d->task});
            }
#endif
            if (private_.process_poll_tasks_.empty()) goto CONTINUE;
            private_.wake_ = true;
            mutex.unlock();
            private_.process_polls();
            mutex.lock();
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
              console_msg("AbstractThread: warning: thread (" + private_.name + ") timer overload, interval: " + std::to_string(t.timeout.count()) + ", expired: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>((now - t.expire + t.timeout)).count()));
              t.expire = now + t.timeout;
            }
            private_.process_timer_tasks_.push({t.id, t.task});
          }
          if (private_.wakeup > t.expire) private_.wakeup = t.expire;
        }
        if (private_.process_timer_tasks_.empty()) goto CONTINUE;
        private_.wake_ = true;
        mutex.unlock();
        private_.process_timers();
        mutex.lock();
        goto CONTINUE;
      }
    }
  }
  if (_state < Running) finishedEvent();
  LockGuard lock(mutex);
  if (_state >= Running) {
    private_.state = _state;
    return;
  }
  if (!private_.tasks.empty()) {
    private_.state = Finalize;
    std::swap(private_.process_tasks_, private_.tasks);
    private_.wake_ = true;
    mutex.unlock();
    private_.process_tasks();
    mutex.lock();
  }
  private_.state = Finished;
  condition_variable.notify_all();
}

void AbstractThread::wake() {
  if (private_.wake_) return;
  warning_if(std::this_thread::get_id() == private_.id) << LogStream::Color::Red << private_.name;
  private_.wake_ = true;
  if (private_.poll_tasks.empty()) {
    condition_variable.notify_all();
    return;
  }
  trace() << LogStream::Color::Cyan << private_.name << private_.id;
#ifndef _WIN32
  #ifndef EVENTFD_WAKE
  char _c = '\x0';
  (void)write(private_.pipe[1], &_c, 1);
  #else
  eventfd_write(WAKE_FD, 1);
  #endif
#else
  ::closesocket(WAKE_FD);
#endif
}

bool AbstractThread::invokeTask(AbstractTask *task) {
  std::lock_guard<std::mutex> lock(mutex);
  warning_if(!private_.state) << LogStream::Color::Red << "thread not running (" + private_.name + ")" << private_.id;
  if (private_.state >= Finalize) {
    lsTrace() << LogStream::Color::Red << "Thread (" + private_.name + ") finished";
    return false;
  }
  if (private_.state == Interrupted) private_.state = Running;
  private_.tasks.push(task);
  wake();
  trace() << LogStream::Color::Green << private_.name << "invoke tasks" << private_.tasks.size();
  return true;
}

void AbstractThread::start() {
  LockGuard lock(mutex);
  if (private_.state && private_.state != Finished) {
    lsWarning() << "thread already running or starting";
    return;
  }
  private_.state = WaitStarted;
  std::thread t {[this]() {
    changeId(std::this_thread::get_id());
    exec();
    changeId({});
  }};
  t.detach();
}

int AbstractThread::workLoad() const {
  LockGuard lock(mutex);
  int n = private_.tasks.size();
  if (private_.wake_) ++n;
  return n;
}

std::thread::id AbstractThread::id() const { return private_.id; }

std::string AbstractThread::name() const { return private_.name; }

int AbstractThread::appendTimer(int ms, AbstractTask *task) {
  int id = 0;
  LockGuard lock(mutex);
  std::vector<Private::Timer>::iterator it = private_.timers.begin();
  for (; it != private_.timers.end(); ++it, ++id)
    if (it->id != id) break;
  std::chrono::milliseconds _ms = std::chrono::milliseconds(ms);
  std::chrono::time_point<std::chrono::steady_clock> _wakeup;
  if (_ms > std::chrono::milliseconds(0)) {
    _wakeup = std::chrono::steady_clock::now() + _ms;
    if (private_.wakeup > _wakeup) {
      private_.wakeup = _wakeup;
      wake();
    }
  }
  private_.timers.emplace(it, Private::Timer {id, _ms, _wakeup, task});
  return id;
}

bool AbstractThread::modifyTimer(int id, int ms) {
  LockGuard lock(mutex);
  std::vector<Private::Timer>::iterator it = std::lower_bound(private_.timers.begin(), private_.timers.end(), id, Private::Compare());
  if (it != private_.timers.end() && it->id == id) {
    it->timeout = std::chrono::milliseconds(ms);
    if (it->timeout > std::chrono::milliseconds(0)) {
      it->expire = std::chrono::steady_clock::now() + it->timeout;
      if (private_.wakeup > it->expire) {
        private_.wakeup = it->expire;
        wake();
      }
    }
    return true;
  }
  lsError() << "timer:" << id << "not found";
  return false;
}

void AbstractThread::removeTimer(int id) {
  std::vector<Private::Timer>::iterator it;
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(mutex);
    it = std::lower_bound(private_.timers.begin(), private_.timers.end(), id, Private::Compare());
    if (it == private_.timers.end() || it->id != id) {
      lsError() << "timer:" << id << "not found";
      return;
    }
    _t = (it->task) ? new InternalTask([p = it->task] { delete p; }) : nullptr;
    private_.timers.erase(it);
  }
  if (!_t) return;
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
}

bool AbstractThread::appendPollDescriptor(int fd, PollEvents events, AbstractTask *task) {
#ifndef EPOLL_WAIT
  LockGuard lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd == fd) {
    delete task;
    lsError() << "append error, descriptor:" << fd;
    return false;
  }
  struct pollfd pollfd;
  pollfd.fd = fd;
  pollfd.events = events;
  Private::PollTask *_d = new Private::PollTask(pollfd.fd, task);
  private_.update_pollfd.push_back({fd, pollfd.events, task, 1});
  wake();
  private_.poll_tasks.insert(it, _d);
#else
  struct epoll_event event;
  event.events = events;
  Private::PollTask *_d = new Private::PollTask(fd, task);
  event.data.ptr = _d;
  if (epoll_ctl(private_.epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
  ERR:
    delete _d;
    lsError() << "append error, descriptor:" << fd;
    return false;
  }
  LockGuard lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd == fd) goto ERR;
  if (private_.poll_tasks.empty()) wake();
  private_.poll_tasks.insert(it, _d);
#endif
  trace();
  return true;
}

bool AbstractThread::modifyPollDescriptor(int fd, PollEvents events) {
#ifndef EPOLL_WAIT
  LockGuard lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
  if (it != private_.poll_tasks.end() && (*it)->fd != fd) {
    lsError("descriptor not found");
    return false;
  }
  struct Private::update_pollfd v;
  v.fd = fd;
  v.events = events;
  v.action = 0;
  private_.update_pollfd.push_back(v);
  wake();
#else
  struct epoll_event event;
  event.events = events;
  {  //lock scope
    LockGuard lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      lsError("descriptor not found");
      return false;
    }
    event.data.ptr = *it;
  }
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_MOD, fd, &event);
#endif
  trace() << fd << static_cast<int>(events);
  return true;
}

void AbstractThread::removePollDescriptor(int fd) {
#ifndef EPOLL_WAIT
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it != private_.poll_tasks.end() && (*it)->fd != fd) {
      lsError("descriptor not found");
      return;
    }
    _t = new InternalTask([p = *it] { delete p; });
    struct Private::update_pollfd v;
    v.fd = fd;
    v.action = -1;
    private_.update_pollfd.push_back(v);
    wake();
    private_.poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
#else
  epoll_ctl(private_.epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  AbstractTask *_t;
  {  //lock scope
    LockGuard lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_.poll_tasks.begin(), private_.poll_tasks.end(), fd, Private::Compare());
    if (it == private_.poll_tasks.end() || (*it)->fd != fd) {
      lsError("descriptor not found");
      return;
    }
    if (private_.poll_tasks.size() == 1) wake();
    _t = new InternalTask([p = *it] { delete p; });
    private_.poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
#endif
  trace() << fd;
}

bool Thread::Compare::operator()(const AbstractSocket *_s1, const AbstractSocket *_s2) const {
  if (_s1->fd_ != _s2->fd_) return _s1->fd_ < _s2->fd_;
  return _s1 < _s2;
}

Thread::Thread(const std::string &name) : AbstractThread(name) { trace(); }

Thread::~Thread() {
  if (running()) {
    quit();
    waitFinished();
  }
  for (; !sockets_.empty();) delete sockets_.back();
  lsTrace();
}

void Thread::startedEvent() {
#ifndef _WIN32
  sigset_t _s;
  sigemptyset(&_s);
  sigaddset(&_s, SIGPIPE);
  sigprocmask(SIG_BLOCK, &_s, nullptr);  //SIGPIPE if close fd while tls handshake, AbstractTlsSocket::acceptEvent()
#endif
}

void Thread::appendSocket(AbstractSocket *_socket) {
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  sockets_.insert(it, _socket);
  trace() << LogStream::Color::Green << _socket->fd_;
}

void Thread::removeSocket(AbstractSocket *_socket) {
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  if (it != sockets_.end() && (*it) == _socket) {
    if (_socket->fd_ >= 0) removePollDescriptor(_socket->fd_);
    sockets_.erase(it);
    _socket->thread_ = nullptr;
    return;
  }
  lsTrace() << LogStream::Color::DarkRed << "not found" << _socket->fd_;
}

AbstractThreadPool::AbstractThreadPool(const std::string &name, AbstractThread *thread) : name_(name) {
  pools_.emplace_back(this);
  thread_ = (thread) ? thread : AbstractThread::currentThread();
  lsTrace("pools: " + std::to_string(pools_.size()));
}

AbstractThreadPool::~AbstractThreadPool() {
  mutex.lock();
  bool b = threads_.size() > 0;
  mutex.unlock();
  if (b) {
    lsWarning() << "destroyed with threads";
    AbstractThreadPool::quit();
  }
  for (std::vector<AbstractThreadPool *>::iterator it = pools_.begin(); it != pools_.end(); it++) {
    if ((*it) == this) {
      pools_.erase(it);
      break;
    }
  }
  lsTrace("pools: " + std::to_string(pools_.size()));
}

void AbstractThreadPool::quit() {
  for (;;) {
    mutex.lock();
    if (threads_.empty()) {
      mutex.unlock();
      break;
    }
    AbstractThread *t = threads_.back();
    threads_.pop_back();
    mutex.unlock();
    t->destroy();
  }
  lsTrace();
}

AbstractThread::LockGuard AbstractThreadPool::threads(std::vector<AbstractThread *> **_threads, AbstractThreadPool *pool) {
  if (!pool) {
    *_threads = &AbstractThread::list_threads;
    return AbstractThread::LockGuard {AbstractThread::list_mutex};
  }
  *_threads = &(pool->threads_);
  return AbstractThread::LockGuard {pool->mutex};
}

void AbstractThreadPool::appendThread(AbstractThread *thread) {
  AbstractThread::LockGuard lock(mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), thread, Compare());
  threads_.insert(it, thread);
  lsTrace("threads: " + std::to_string(threads_.size()));
}

void AbstractThreadPool::removeThread(AbstractThread *thread) {
  AbstractThread::LockGuard lock(mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), thread, Compare());
  if (it != threads_.end() && (*it) == thread) threads_.erase(it);
  else { lsDebug() << LogStream::Color::Red << "thread not found: (" + thread->name() + ')'; }
  lsTrace("threads: " + std::to_string(threads_.size()));
}

AbstractThreadPool::Thread::Thread(const std::string &name, AbstractThreadPool *_pool, bool autoStart) : AsyncFw::Thread(name), pool(_pool) {
  pool->appendThread(this);
  if (autoStart) start();
  lsTrace();
}

AbstractThreadPool::Thread::~Thread() {
  LockGuard lock(mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(pool->threads_.begin(), pool->threads_.end(), this, AbstractThreadPool::Compare());
  if (it != pool->threads_.end() && (*it) == this) {
    pool->threads_.erase(it);
    lsError() << "thread not removed from pool";
  }
  lsTrace();
}

void AbstractThreadPool::Thread::destroy() {
  lsTrace();
  AbstractThread::quit();
  waitFinished();

  if (std::this_thread::get_id() != this->private_.id) {
    delete this;
    return;
  }
  pool->thread_->invokeMethod([this]() { delete this; });
}

void AbstractThreadPool::Thread::quit() {
  checkDifferentThread();
  pool->removeThread(this);
  destroy();
}
