/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/LogStream.h"
#include "ThreadPool.h"

using namespace AsyncFw;

struct AbstractThreadPool::Private {
  static inline struct List : public std::vector<AbstractThreadPool *> {
    friend AbstractThreadPool;
    ~List();
  } list_ __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 2)));

  struct Compare {
    bool operator()(const AbstractThread *t1, const AbstractThread *t2) const { return t1 < t2; }
    bool operator()(const AbstractThreadPool *p1, const AbstractThreadPool *p2) const { return p1 < p2; }
  };
  std::string name_;
};

AbstractThreadPool::Private::List::~List() {
  if (!empty()) {
    lsError() << "thread pool list not empty:" << size();
    while (!empty()) {
      AbstractThreadPool *_p = back();
      delete _p;
    }
  }
  lsDebug() << LogStream::Color::DarkMagenta << size();
}

std::vector<AbstractThreadPool *> AbstractThreadPool::pools() { return Private::list_; }

AbstractThreadPool::AbstractThreadPool(const std::string &name) : private_(*new Private) {
  thread_ = AbstractThread::currentThread();
  private_.name_ = name;
  std::vector<AbstractThreadPool *>::iterator it = std::lower_bound(Private::list_.begin(), Private::list_.end(), this, Private::Compare());
  Private::list_.insert(it, this);
  lsTrace("pools: " + std::to_string(Private::list_.size()));
}

AbstractThreadPool::~AbstractThreadPool() {
  mutex.lock();
  bool b = threads_.size() > 0;
  mutex.unlock();
  if (b) {
    lsDebug() << LogStream::Color::Red << "destroyed with threads";
    AbstractThreadPool::quit();
  }
  std::vector<AbstractThreadPool *>::iterator it = std::lower_bound(Private::list_.begin(), Private::list_.end(), this, Private::Compare());
  if (it != Private::list_.end() && (*it) == this) Private::list_.erase(it);
  else { lsError() << "pool not found in list"; }

  delete &private_;
  lsTrace("pools: " + std::to_string(Private::list_.size()));
}

void AbstractThreadPool::quit() {
  Thread *t;
  for (;;) {
    {  //lock scope
      std::lock_guard<std::mutex> lock(mutex);
      if (threads_.empty()) break;
      t = threads_.back();
    }
    t->destroy();
  }
  lsTrace();
}

std::string AbstractThreadPool::name() const { return private_.name_; }

AbstractThread::LockGuard AbstractThreadPool::threads(std::vector<AbstractThreadPool::Thread *> **_threads) {
  *_threads = &threads_;
  return AbstractThread::LockGuard {mutex};
}

void AbstractThreadPool::appendThread(AbstractThreadPool::Thread *thread) {
  AbstractThread::LockGuard lock(mutex);
  std::vector<AbstractThreadPool::Thread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), thread, Private::Compare());
  threads_.insert(it, thread);
  lsTrace("threads: " + std::to_string(threads_.size()));
}

void AbstractThreadPool::removeThread(AbstractThreadPool::Thread *thread) {
  AbstractThread::LockGuard lock(mutex);
  std::vector<AbstractThreadPool::Thread *>::iterator it = std::lower_bound(threads_.begin(), threads_.end(), thread, Private::Compare());
  if (it != threads_.end() && (*it) == thread) threads_.erase(it);
  else { lsDebug() << LogStream::Color::Red << "thread not found: (" + thread->name() + ')'; }
  lsTrace("threads: " + std::to_string(threads_.size()));
}

AbstractThreadPool::Thread::Thread(const std::string &name, AbstractThreadPool *_pool) : AsyncFw::Thread(name), pool(_pool) {
  pool->appendThread(this);
  lsTrace();
}

void AbstractThreadPool::Thread::destroy() {
  pool->removeThread(this);
  AbstractThread::quit();
  AbstractTask *_t = new Function<>::Value([p = this]() {
    p->waitFinished();
    delete p;
  });
  if (!pool->thread_->invokeTask(_t)) {
    lsDebug() << LogStream::Color::Red << "pool thread not running" << '(' + pool->thread_->name() + ')';
    (*_t)();
    delete _t;
  }
  lsTrace();
}

Instance<ThreadPool> ThreadPool::instance_ {"ThreadPool"};

ThreadPool::ThreadPool(const std::string &name, int workThreads) : AbstractThreadPool(name), workThreadsSize_(workThreads) { lsTrace() << "created" << name; }

ThreadPool::~ThreadPool() {
  if (instance_.value == this) instance_.value = nullptr;
  lsTrace() << "destroyed" << name();
}

ThreadPool::Thread *ThreadPool::createThread(const std::string &_name) {
  Thread *thread = new Thread((!_name.empty()) ? _name : name() + " thread", this);
  thread->start();
  lsTrace() << "created thread (" + thread->name() + ')';
  return thread;
}

void ThreadPool::removeThread(AbstractThreadPool::Thread *thread) {
  std::vector<AbstractThreadPool::Thread *>::iterator it = std::find(workThreads_.begin(), workThreads_.end(), thread);
  if (it != workThreads_.end()) workThreads_.erase(it);
  AbstractThreadPool::removeThread(thread);
  lsTrace();
}

void ThreadPool::quit() {
  AbstractThreadPool::quit();
  lsTrace();
}

AbstractThreadPool::Thread *ThreadPool::getThread() {
  int _s = workThreads_.size();
  if (_s < workThreadsSize_) {
    for (int i = 0; i != _s; ++i)
      if (workThreads_[i]->workLoad() == 0) return workThreads_[i];

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
