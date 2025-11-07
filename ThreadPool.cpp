#include "core/LogStream.h"
#include "ThreadPool.h"

#define DEFAULT_WT_SIZE 2

using namespace AsyncFw;

ThreadPool::Thread::~Thread() { logTrace() << "Destroyed thread \'" + name() + "\', count:" << AbstractThreadPool::threadCount(pool); }

ThreadPool::Thread *ThreadPool::currentThread() {
  AbstractThread *_t = AbstractThread::currentThread();
  for (const AbstractThread *t : threads_) {
    if (t == _t) return static_cast<Thread *>(_t);
  }
  return nullptr;
}

ThreadPool::ThreadPool(const std::string & name) : AbstractThreadPool(name) {
  instance_ = this;
  logTrace() << "Created";
}

ThreadPool::~ThreadPool() { logTrace() << "Destroyed"; }

ThreadPool::Thread *ThreadPool::createThread(const std::string &_name) {
  int i = AbstractThreadPool::threadCount(this);
  Thread *thread = new Thread((!_name.empty()) ? _name : name() + " thread", this);
  logTrace() << "Created thread \'" + thread->name() + "\', count:" << i + 1;
  return thread;
}

void ThreadPool::quit() {
  logTrace() << "Wait for quit";
  AbstractThreadPool::quit();
  logTrace() << "Quited";
}

ThreadPool::Thread *ThreadPool::getThread() {
  int _s = workThreads_.size();
  if (_s < DEFAULT_WT_SIZE) {
    for (int i = 0; i != _s; ++i)
      if (static_cast<ThreadPool::Thread *>(workThreads_[i])->queuedTasks() == 0) return workThreads_[i];

    workThreads_.emplace_back(createThread("Work: " + std::to_string(_s)));
    return workThreads_.back();
  }
  ThreadPool::Thread *_t = nullptr;
  int _tasks = INT_MAX;
  for (int i = 0; i != _s; ++i) {
    int _qt = static_cast<ThreadPool::Thread *>(workThreads_[i])->queuedTasks();
    if (_qt < _tasks) {
      _tasks = _qt;
      _t = static_cast<ThreadPool::Thread *>(workThreads_[i]);
    }
  }
  return _t;
}
