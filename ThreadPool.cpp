#include <algorithm>
#include "core/LogStream.h"
#include "ThreadPool.h"

using namespace AsyncFw;

ThreadPool::Thread::~Thread() {
  std::vector<Thread *>::iterator it = std::find(static_cast<ThreadPool *>(pool)->workThreads_.begin(), static_cast<ThreadPool *>(pool)->workThreads_.end(), this);
  if (it != static_cast<ThreadPool *>(pool)->workThreads_.end()) static_cast<ThreadPool *>(pool)->workThreads_.erase(it);
  lsTrace() << "destroyed thread \'" + name() + "\'";
}

ThreadPool::ThreadPool(const std::string &name, int workThreads) : AbstractThreadPool(name), workThreadsSize(workThreads) {
  if (!instance_) instance_ = this;
  else { lsWarning("instance already exists"); }
  lsTrace() << "created";
}

ThreadPool::~ThreadPool() { lsTrace() << "destroyed"; }

ThreadPool::Thread *ThreadPool::createThread(const std::string &_name) {
  Thread *thread = new Thread((!_name.empty()) ? _name : name() + " thread", this);
  lsTrace() << "created thread \'" + thread->name() + "\'";
  return thread;
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
