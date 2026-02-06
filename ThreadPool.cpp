#include <algorithm>
#include "core/LogStream.h"
#include "ThreadPool.h"

using namespace AsyncFw;

AbstractThreadPool::AbstractThreadPool(const std::string &name) : name_(name) {
  pools_.emplace_back(this);
  thread_ = AbstractThread::currentThread();
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
  AbstractThread *t;
  for (;;) {
    {  //lock scope
      std::lock_guard<std::mutex> lock(mutex);
      if (threads_.empty()) break;
      t = threads_.back();
      threads_.pop_back();
    }
    t->destroy();
  }
  lsTrace();
}

AbstractThread::LockGuard AbstractThreadPool::threads(std::vector<AbstractThread *> **_threads) {
  *_threads = &threads_;
  return AbstractThread::LockGuard {mutex};
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

AbstractThreadPool::Thread::Thread(const std::string &name, AbstractThreadPool *_pool) : AsyncFw::Thread(name), pool(_pool) {
  pool->appendThread(this);
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
  AbstractTask *_t = new Task([p = this]() { delete p; });
  if (!pool->thread_->invokeTask(_t)) {
    _t->invoke();
    delete _t;
  }
}

void AbstractThreadPool::Thread::quit() {
  checkDifferentThread();
  pool->removeThread(this);
  destroy();
}

ThreadPool::Thread::~Thread() { lsTrace() << "destroyed thread (" + name() + ')'; }

ThreadPool::ThreadPool(const std::string &name, int workThreads) : AbstractThreadPool(name), workThreadsSize(workThreads) {
  if (!instance_) instance_ = this;
  else { lsWarning("instance already exists"); }
  lsTrace() << "created";
}

ThreadPool::~ThreadPool() { lsTrace() << "destroyed"; }

ThreadPool::Thread *ThreadPool::createThread(const std::string &_name) {
  Thread *thread = new Thread((!_name.empty()) ? _name : name() + " thread", this);
  thread->start();
  lsTrace() << "created thread (" + thread->name() + ')';
  return thread;
}

void ThreadPool::removeThread(AbstractThread *thread) {
  std::vector<Thread *>::iterator it = std::find(workThreads_.begin(), workThreads_.end(), thread);
  if (it != workThreads_.end()) workThreads_.erase(it);
  AbstractThreadPool::removeThread(thread);
  lsTrace();
}

void ThreadPool::quit() {
  AbstractThreadPool::quit();
  lsTrace();
}

ThreadPool::Thread *ThreadPool::getThread() {
  int _s = workThreads_.size();
  if (_s < workThreadsSize) {
    for (int i = 0; i != _s; ++i)
      if (static_cast<ThreadPool::Thread *>(workThreads_[i])->workLoad() == 0) return workThreads_[i];

    workThreads_.emplace_back(createThread("Work-" + std::to_string(_s)));
    return workThreads_.back();
  }
  ThreadPool::Thread *_t = nullptr;
  int _load = std::numeric_limits<int>::max();
  for (int i = 0; i != _s; ++i) {
    int _l = static_cast<ThreadPool::Thread *>(workThreads_[i])->workLoad();
    if (_l < _load) {
      _load = _l;
      _t = static_cast<ThreadPool::Thread *>(workThreads_[i]);
    }
  }
  return _t;
}
