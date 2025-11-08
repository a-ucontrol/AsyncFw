#include <queue>
#include <signal.h>
#include "LogStream.h"
#include "Thread.h"
#include "Socket.h"

#define EVENTFD_WAKE
#define EPOLL_WAIT

#ifdef EXTEND_THREAD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
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
  #define WAKE_FD private_->wake_task.fd
#else
  #define WAKE_FD private_->fds_[0].fd
#endif

#include "console_msg.hpp"

using namespace AsyncFw;
AbstractTask::~AbstractTask() {
#if 0
  console_msg(__PRETTY_FUNCTION__);
#endif
}

bool AbstractThread::Compare::operator()(const AbstractThread *t, std::thread::id id) const { return t->id_ < id; }
bool AbstractThread::Compare::operator()(const AbstractThread *t1, const AbstractThread *t2) const {
  if (t1->id_ != t2->id_) return t1->id_ < t2->id_;
  return t1 < t2;
}

AbstractThread::AbstractThread(const std::string &_name) : name_(_name) {
  std::lock_guard<MutexType> lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), this, Compare());
  threads_.insert(it, this);
  trace() << "threads:" << std::to_string(threads_.size());
}

AbstractThread::~AbstractThread() {
  std::lock_guard<MutexType> lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), this, Compare());
  if (it != threads_.end() && (*it) == this) threads_.erase(it);
  else { ucError() << "thread not found"; }
  trace() << "threads:" << std::to_string(threads_.size());
}

void AbstractThread::setId(std::thread::id _id) {
  std::lock_guard<MutexType> lock(list_mutex);
  std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), this, Compare());
  if (it != threads_.end() && (*it) == this) threads_.erase(it);
  else { ucError() << "thread not found"; }
  id_ = _id;
  it = std::lower_bound(threads_.begin(), threads_.end(), this, Compare());
  threads_.insert(it, this);
}

int AbstractThread::appendTimer(int, AbstractTask *) {
  ucWarning("not implemented");
  return -1;
}

bool AbstractThread::modifyTimer(int, int) {
  ucWarning("not implemented");
  return false;
}

void AbstractThread::removeTimer(int) { ucWarning("not implemented"); }

bool AbstractThread::appendPollDescriptor(int, PollEvents, AbstractTask *) {
  ucWarning("not implemented");
  return false;
}

bool AbstractThread::modifyPollDescriptor(int, PollEvents) {
  ucWarning("not implemented");
  return false;
}

void AbstractThread::removePollDescriptor(int) { ucWarning("not implemented"); }

bool AbstractThread::running() {
  std::lock_guard<MutexType> lock(mutex);
  return state > WaitStarted && state != Finished;
}

AbstractThread *AbstractThread::currentThread() {
  std::thread::id _id = std::this_thread::get_id();
  {  //lock scope
    std::lock_guard<MutexType> lock(list_mutex);
    std::vector<AbstractThread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), _id, Compare());
    if (it != threads_.end() && (*it)->id_ == _id) return (*it);
  }
  ucError() << "thread not found:" << _id;
  return nullptr;
}

struct ExecLoopThread::Private {
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

  std::queue<AbstractTask *> process_tasks;
  std::queue<ProcessTimerTask> process_timer_tasks;
  std::queue<ProcessPollTask> process_poll_tasks;
};

ExecLoopThread::ExecLoopThread(const std::string &name) : AbstractThread(name) {
  private_ = new Private;

#ifndef EPOLL_WAIT
  pollfd _w;
  _w.events = POLLIN;
  private_->fds_.push_back(_w);
  #ifndef _WIN32
    #ifndef EVENTFD_WAKE
  if (::pipe(private_->pipe)) console_msg("ExecLoopThread: error create pipe");
  WAKE_FD = private_->pipe[0];
    #else
  WAKE_FD = eventfd(0, EFD_NONBLOCK);
    #endif
  #else
  WAKE_FD = socket(AF_INET, 0, 0);
  #endif
#else
  private_->epoll_fd = epoll_create1(0);
  struct epoll_event event;
  event.events = EPOLLIN;
  #ifdef EVENTFD_WAKE
  private_->wake_task.fd = eventfd(0, EFD_NONBLOCK);
  #else
  if (::pipe(private_->pipe)) console_msg("ExecLoopThread: error create pipe");
  private_->wake_task.fd = private_->pipe[0];
  #endif
  private_->wake_task.task = nullptr;
  event.data.ptr = &private_->wake_task;
  epoll_ctl(private_->epoll_fd, EPOLL_CTL_ADD, WAKE_FD, &event);
#endif

  ucTrace() << name;
}

ExecLoopThread::~ExecLoopThread() {
#ifndef _WIN32
  ::close(WAKE_FD);
  #ifndef EVENTFD_WAKE
  ::close(private_->pipe[1]);
  #endif
  #ifdef EPOLL_WAIT
  ::close(private_->epoll_fd);
  #endif
#else
  ::closesocket(WAKE_FD);
#endif

  warning_if(std::this_thread::get_id() == id_) << LogStream::Color::Red << "executed from own thread (" + name_ + ")";
  if (running()) {
    logWarning() << "Destroing running thread";
    quit();
    waitFinished();
  }

  ucTrace() << name_ << "-" << private_->tasks.size() << private_->timers.size() << private_->poll_tasks.size() << private_->process_tasks.size() << private_->process_timer_tasks.size() << private_->process_poll_tasks.size();

  if (!private_->tasks.empty()) {
    ucDebug() << LogStream::Color::Red << "Task list not empty";
    std::swap(private_->process_tasks, private_->tasks);
    process_tasks();
  }

  delete private_;
}

void ExecLoopThread::process_tasks() {
  for (; !private_->process_tasks.empty();) {
    AbstractTask *_t = private_->process_tasks.front();
    private_->process_tasks.pop();
    _t->invoke();
    delete _t;
  }
}

void ExecLoopThread::process_polls() {
  for (; !private_->process_poll_tasks.empty();) {
    Private::ProcessPollTask _pt = private_->process_poll_tasks.front();
    private_->process_poll_tasks.pop();
    static_cast<InternalPoolTask<AbstractThread::PollEvents> *>(_pt.task)->e_ = static_cast<AbstractThread::PollEvents>(_pt.events);
    _pt.task->invoke();
  }
}

void ExecLoopThread::process_timers() {
  for (; !private_->process_timer_tasks.empty();) {
    Private::ProcessTimerTask _tt = private_->process_timer_tasks.front();
    private_->process_timer_tasks.pop();
    _tt.task->invoke();
  }
}

void ExecLoopThread::exec() {
  int _state;

  {  //lock scope
    std::lock_guard<MutexType> lock(mutex);
    warning_if(private_->process_tasks.size() || private_->process_poll_tasks.size() || private_->process_timer_tasks.size()) << LogStream::Color::Red << "not empty" << private_->process_tasks.size() << private_->process_poll_tasks.size() << private_->process_timer_tasks.size();
    _state = (state != Finished) ? state : None;
    if (_state >= Running) {  //nested exec
      trace() << LogStream::Color::Red << "nested" << private_->process_tasks.size() << private_->process_poll_tasks.size() << private_->process_timer_tasks.size();
      if (!private_->process_tasks.empty()) {
        mutex.unlock();
        process_tasks();
        mutex.lock();
      } else if (!private_->process_poll_tasks.empty()) {
        mutex.unlock();
        process_polls();
        mutex.lock();
      } else if (!private_->process_timer_tasks.empty()) {
        mutex.unlock();
        process_timers();
        mutex.lock();
      }
    }

    std::swap(private_->process_tasks, private_->tasks);  //take exists tasks
    state = Running;
    if (_state < Running) condition_variable.notify_all();
  }
  if (_state < Running) {
    setId(std::this_thread::get_id());
    startedEvent();
  }
  for (;;) {
    process_tasks();
    {  //lock scope, wait new tasks or wakeup
      std::unique_lock<std::mutex> lock(mutex);
    CONTINUE:
      if (!private_->tasks.empty()) {
        std::swap(private_->process_tasks, private_->tasks);  //take new tasks
        continue;
      }
      if (state == WaitFinished) break;
      if (state == WaitInterrupted) state = Interrupted;

      condition_variable.notify_all();
      std::chrono::time_point now = std::chrono::steady_clock::now();

      if (private_->wakeup > now) {
        private_->wake_ = false;
        if (private_->poll_tasks.empty()) {
          if (condition_variable.wait_until(lock, private_->wakeup) == std::cv_status::no_timeout) {
            private_->wake_ = true;
            goto CONTINUE;
          }
        } else {
          uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(private_->wakeup - now).count();
#ifndef EPOLL_WAIT
          if (!private_->update_pollfd.empty()) {
            for (const struct Private::update_pollfd &pfd : private_->update_pollfd) {
              std::vector<pollfd>::iterator it = std::lower_bound(private_->fds_.begin() + 1, private_->fds_.end(), pfd.fd, Private::Compare());
              if (pfd.action == 0) {
                it->events = pfd.events;
                continue;
              }
              if (pfd.action == 1) {
                private_->fdts_.insert(private_->fdts_.begin() + (it - 1 - private_->fds_.begin()), pfd.task);
                pollfd _pfd;
                _pfd.fd = pfd.fd;
                _pfd.events = pfd.events;
                private_->fds_.insert(it, _pfd);
                continue;
              }
              if (pfd.action == -1) {
                private_->fdts_.erase(private_->fdts_.begin() + (it - 1 - private_->fds_.begin()));
                private_->fds_.erase(it);
                continue;
              }
            }
            private_->update_pollfd.clear();
          }

          mutex.unlock();
          int r = poll(private_->fds_.data(), private_->fds_.size(), ms);
          mutex.lock();

          int i = 0;
          if (private_->wake_) {
            trace() << LogStream::Color::Magenta << "waked" << name_ << id_ << private_->fds_[0].revents << r;
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
            if (private_->fds_[0].revents) {
              if (r == 1) goto CONTINUE;
              i = 1;
            }
          }

          if (r > 0) {
            for (std::vector<pollfd>::const_iterator it = private_->fds_.begin() + 1; i != r; ++it)
              if (it->revents) {
                private_->process_poll_tasks.push({it->fd, it->revents, *(private_->fdts_.begin() + (it - 1 - private_->fds_.begin()))});
                i++;
              }
#else
          struct epoll_event event[EPOLL_WAIT_EVENTS];
          mutex.unlock();
          int r = epoll_wait(private_->epoll_fd, event, EPOLL_WAIT_EVENTS, ms);
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
                trace() << LogStream::Color::Magenta << "waked" << name_ << id_ << event[i].events << private_->wake_;
                continue;
              }
              Private::PollTask *_d = static_cast<Private::PollTask *>(event[i].data.ptr);
              private_->process_poll_tasks.push({_d->fd, event[i].events, _d->task});
            }
#endif
            if (private_->process_poll_tasks.empty()) goto CONTINUE;
            private_->wake_ = true;
            mutex.unlock();
            process_polls();
            mutex.lock();
            goto CONTINUE;
          }
        }
      }

      private_->wakeup = std::chrono::time_point<std::chrono::steady_clock>::max();
      if (!private_->timers.empty()) {
        for (Private::Timer &t : private_->timers) {
          if (t.timeout == std::chrono::milliseconds(0)) continue;
          if (t.expire <= now) {
            t.expire += t.timeout;
            if (t.expire <= now) {
              console_msg("ExecLoopThread: warning: thread (" + name_ + ") timer overload, interval: " + std::to_string(t.timeout.count()) + ", expired: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>((now - t.expire + t.timeout)).count()));
              t.expire = now + t.timeout;
            }
            private_->process_timer_tasks.push({t.id, t.task});
          }
          if (private_->wakeup > t.expire) private_->wakeup = t.expire;
        }
        if (private_->process_timer_tasks.empty()) goto CONTINUE;
        private_->wake_ = true;
        mutex.unlock();
        process_timers();
        mutex.lock();
        goto CONTINUE;
      }
    }
  }
  if (_state < Running) finishedEvent();
  std::lock_guard<MutexType> lock(mutex);
  if (_state >= Running) {
    state = _state;
    return;
  }
  if (!private_->tasks.empty()) {
    state = Finalize;
    std::swap(private_->process_tasks, private_->tasks);
    private_->wake_ = true;
    mutex.unlock();
    process_tasks();
    mutex.lock();
  }
  state = Finished;
  condition_variable.notify_all();
  setId({});
}

void ExecLoopThread::wake() {
  if (private_->wake_) return;
  warning_if(std::this_thread::get_id() == id_) << LogStream::Color::Red << name_;
  private_->wake_ = true;
  if (private_->poll_tasks.empty()) {
    condition_variable.notify_all();
    return;
  }
  trace() << LogStream::Color::Cyan << name_ << id_;
#ifndef _WIN32
  #ifndef EVENTFD_WAKE
  char _c = '\x0';
  (void)write(private_->pipe[1], &_c, 1);
  #else
  eventfd_write(WAKE_FD, 1);
  #endif
#else
  ::closesocket(WAKE_FD);
#endif
}

bool ExecLoopThread::invokeTask(AbstractTask *task) {
  std::lock_guard<std::mutex> lock(mutex);
  warning_if(!state) << LogStream::Color::Red << "thread not running (" + name_ + ")" << id_;
  if (state >= Finalize) {
    ucTrace() << LogStream::Color::Red << "Thread (" + name_ + ") finished";
    return false;
  }
  if (state == Interrupted) state = Running;
  private_->tasks.push(task);
  wake();
  return true;
}

bool ExecLoopThread::interruptRequested() const {
  std::lock_guard<MutexType> lock(mutex);
  return state == WaitInterrupted || state == WaitFinished;
}

void ExecLoopThread::requestInterrupt() {
  std::lock_guard<MutexType> lock(mutex);
  if (state != Interrupted) {
    state = WaitInterrupted;
    wake();
  }
}

void ExecLoopThread::start() {
  std::lock_guard<MutexType> lock(mutex);
  if (state && state != Finished) {
    logWarning() << "Thread already running or starting";
    return;
  }
  state = WaitStarted;
  std::thread t {[this]() { exec(); }};
  t.detach();
}

void ExecLoopThread::quit() {
  ucDebug() << name() << this;
  std::lock_guard<MutexType> lock(mutex);
  if (!state || state == Finished) {
    logWarning() << "Thread already finished or not started";
    return;
  }
  state = WaitFinished;
  wake();
}

void ExecLoopThread::waitInterrupted() const {
  checkDifferentThread();
  std::unique_lock<MutexType> lock(mutex);
  if (state == Interrupted) return;
  if (state != WaitInterrupted) {
    logWarning("Interrupt request not set");
    return;
  }
  while (state == WaitInterrupted) condition_variable.wait(lock);
}

void ExecLoopThread::waitFinished() const {
  checkDifferentThread();
  std::unique_lock<MutexType> lock(mutex);
  if (!state) {
    logWarning() << "Thread not running";
    return;
  }
  while (state != Finished) condition_variable.wait(lock);
}

int ExecLoopThread::queuedTasks() const {
  std::lock_guard<MutexType> lock(mutex);
  return private_->tasks.size();
}

int ExecLoopThread::appendTimer(int ms, AbstractTask *task) {
  int id = 0;
  std::lock_guard<MutexType> lock(mutex);
  std::vector<Private::Timer>::iterator it = private_->timers.begin();
  for (; it != private_->timers.end(); ++it, ++id)
    if (it->id != id) break;
  std::chrono::milliseconds _ms = std::chrono::milliseconds(ms);
  std::chrono::time_point<std::chrono::steady_clock> _wakeup;
  if (_ms > std::chrono::milliseconds(0)) {
    _wakeup = std::chrono::steady_clock::now() + _ms;
    if (private_->wakeup > _wakeup) {
      private_->wakeup = _wakeup;
      wake();
    }
  }
  private_->timers.emplace(it, Private::Timer {id, _ms, _wakeup, task});
  return id;
}

bool ExecLoopThread::modifyTimer(int id, int ms) {
  std::lock_guard<MutexType> lock(mutex);
  std::vector<Private::Timer>::iterator it = std::lower_bound(private_->timers.begin(), private_->timers.end(), id, Private::Compare());
  if (it != private_->timers.end() && it->id == id) {
    it->timeout = std::chrono::milliseconds(ms);
    if (it->timeout > std::chrono::milliseconds(0)) {
      it->expire = std::chrono::steady_clock::now() + it->timeout;
      if (private_->wakeup > it->expire) {
        private_->wakeup = it->expire;
        wake();
      }
    }
    return true;
  }
  ucError() << "timer:" << id << "not found";
  return false;
}

void ExecLoopThread::removeTimer(int id) {
  std::vector<Private::Timer>::iterator it;
  AbstractTask *_t;
  {  //lock scope
    std::lock_guard<MutexType> lock(mutex);
    it = std::lower_bound(private_->timers.begin(), private_->timers.end(), id, Private::Compare());
    if (it == private_->timers.end() || it->id != id) {
      ucError() << "timer:" << id << "not found";
      return;
    }
    _t = (it->task) ? new InternalTask([p = it->task] { delete p; }) : nullptr;
    private_->timers.erase(it);
  }
  if (!_t) return;
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
}

bool ExecLoopThread::appendPollDescriptor(int fd, PollEvents events, AbstractTask *task) {
#ifndef EPOLL_WAIT
  std::lock_guard<MutexType> lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
  if (it != private_->poll_tasks.end() && (*it)->fd == fd) {
    delete task;
    logError() << "Append error, descriptor:" << fd;
    return false;
  }
  struct pollfd pollfd;
  pollfd.fd = fd;
  pollfd.events = events;
  Private::PollTask *_d = new Private::PollTask(pollfd.fd, task);
  private_->update_pollfd.push_back({fd, pollfd.events, task, 1});
  wake();
  private_->poll_tasks.insert(it, _d);
#else
  struct epoll_event event;
  event.events = events;
  Private::PollTask *_d = new Private::PollTask(fd, task);
  event.data.ptr = _d;
  if (epoll_ctl(private_->epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
  ERR:
    delete _d;
    logError() << "Append error, descriptor:" << fd;
    return false;
  }
  std::lock_guard<MutexType> lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
  if (it != private_->poll_tasks.end() && (*it)->fd == fd) goto ERR;
  if (private_->poll_tasks.empty()) wake();
  private_->poll_tasks.insert(it, _d);
#endif
  trace();
  return true;
}

bool ExecLoopThread::modifyPollDescriptor(int fd, PollEvents events) {
#ifndef EPOLL_WAIT
  std::lock_guard<MutexType> lock(mutex);
  std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
  if (it != private_->poll_tasks.end() && (*it)->fd != fd) {
    logError("Modify: descriptor not found");
    return false;
  }
  struct Private::update_pollfd v;
  v.fd = fd;
  v.events = events;
  v.action = 0;
  private_->update_pollfd.push_back(v);
  wake();
#else
  struct epoll_event event;
  event.events = events;
  {  //lock scope
    std::lock_guard<MutexType> lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
    if (it == private_->poll_tasks.end() || (*it)->fd != fd) {
      logError("Modify: descriptor not found");
      return false;
    }
    event.data.ptr = *it;
  }
  epoll_ctl(private_->epoll_fd, EPOLL_CTL_MOD, fd, &event);
#endif
  trace() << fd << static_cast<int>(events);
  return true;
}

void ExecLoopThread::removePollDescriptor(int fd) {
#ifndef EPOLL_WAIT
  AbstractTask *_t;
  {  //lock scope
    std::lock_guard<MutexType> lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
    if (it != private_->poll_tasks.end() && (*it)->fd != fd) {
      logError("Remove: descriptor not found");
      return;
    }
    _t = new InternalTask([p = *it] { delete p; });
    struct Private::update_pollfd v;
    v.fd = fd;
    v.action = -1;
    private_->update_pollfd.push_back(v);
    wake();
    private_->poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
#else
  epoll_ctl(private_->epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  AbstractTask *_t;
  {  //lock scope
    std::lock_guard<MutexType> lock(mutex);
    std::vector<Private::PollTask *>::iterator it = std::lower_bound(private_->poll_tasks.begin(), private_->poll_tasks.end(), fd, Private::Compare());
    if (it == private_->poll_tasks.end() || (*it)->fd != fd) {
      logError("Remove: descriptor not found");
      return;
    }
    if (private_->poll_tasks.size() == 1) wake();
    _t = new InternalTask([p = *it] { delete p; });
    private_->poll_tasks.erase(it);
  }
  if (!invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
#endif
  trace() << fd;
}

void ExecLoopThread::destroy() { ucError("not implemented"); }

bool SocketThread::Compare::operator()(const AbstractSocket *_s1, const AbstractSocket *_s2) const {
  if (_s1->fd_ != _s2->fd_) return _s1->fd_ < _s2->fd_;
  return _s1 < _s2;
}

SocketThread::SocketThread(const std::string &name) : ExecLoopThread(name) { trace(); }

SocketThread::~SocketThread() {
  if (running()) {
    quit();
    waitFinished();
  }
  for (; !sockets_.empty();) delete sockets_.back();
  ucTrace();
}

void SocketThread::startedEvent() {
#ifndef _WIN32
  sigset_t _s;
  sigemptyset(&_s);
  sigaddset(&_s, SIGPIPE);
  sigprocmask(SIG_BLOCK, &_s, nullptr);  //SIGPIPE if close fd while tls handshake, AbstractTlsSocket::acceptEvent()
#endif
}

void SocketThread::appendSocket(AbstractSocket *_socket) {
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  sockets_.insert(it, _socket);
  trace() << LogStream::Color::Green << _socket->fd_;
}

void SocketThread::removeSocket(AbstractSocket *_socket) {
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  if (it != sockets_.end() && (*it) == _socket) {
    if (_socket->fd_ >= 0) removePollDescriptor(_socket->fd_);
    sockets_.erase(it);
    _socket->thread_ = nullptr;
    return;
  }
  ucTrace() << LogStream::Color::DarkRed << "not found" << _socket->fd_;
}

AbstractThreadPool::AbstractThreadPool(const std::string &name, AbstractThread *thread) : name_(name) {
  pools_.emplace_back(this);
  thread_ = (thread) ? thread : AbstractThread::currentThread();
  ucTrace("pools: " + std::to_string(pools_.size()));
}

AbstractThreadPool::~AbstractThreadPool() {
  mutex.lock();
  bool b = threads_.size() > 0;
  mutex.unlock();
  if (b) {
    ucWarning() << "destroyed with threads";
    AbstractThreadPool::quit();
  }
  for (std::vector<AbstractThreadPool *>::iterator it = pools_.begin(); it != pools_.end(); it++) {
    if ((*it) == this) {
      pools_.erase(it);
      break;
    }
  }
  ucTrace("pools: " + std::to_string(pools_.size()));
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
  ucTrace();
}

AbstractThread::LockGuard AbstractThreadPool::threads(std::vector<AbstractThread *> **_threads, AbstractThreadPool *pool) {
  if (!pool) {
    *_threads = &AbstractThread::threads_;
    return AbstractThread::LockGuard {AbstractThread::list_mutex};
  }
  *_threads = &(pool->threads_);
  return AbstractThread::LockGuard {pool->mutex};
}

void AbstractThreadPool::removeThread(AbstractThread *thread) {
  std::lock_guard<AbstractThread::MutexType> lock(mutex);
  for (std::vector<AbstractThread *>::iterator it = threads_.begin(); it != threads_.end(); it++) {
    if ((*it) == thread) {
      threads_.erase(it);
      return;
    }
  }
}

AbstractThreadPool::Thread::Thread(const std::string &name, AbstractThreadPool *_pool, bool autoStart) : SocketThread(name), pool(_pool) {
  pool->mutex.lock();
  pool->threads_.emplace_back(this);
  ucTrace("threads: " + std::to_string(pool->threads_.size()));
  pool->mutex.unlock();
  if (autoStart) start();
}

AbstractThreadPool::Thread::~Thread() {
#ifndef uC_NO_TRACE
  pool->mutex.lock();
  ucTrace("threads: " + std::to_string(pool->threads_.size()));
  pool->mutex.unlock();
#endif
}

void AbstractThreadPool::Thread::destroy() {
  ucTrace();
  ExecLoopThread::quit();
  waitFinished();

  if (std::this_thread::get_id() != this->id_) {
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

void ExecLoopThread::Holder::complete() {
  waiting = false;
  thread->quit();
}

void ExecLoopThread::Holder::wait() {
  waiting = true;
  thread = static_cast<ExecLoopThread *>(AbstractThread::currentThread());
  bool _q = false;
  for (;;) {
    thread->exec();
    if (!waiting) {
      if (thread->state == AbstractThread::Finished) thread->state = AbstractThread::None;
      break;
    }
    _q = true;
  }
  if (_q) thread->quit();
}
