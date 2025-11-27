#include "core/Thread.h"
#include "core/LogStream.h"

#include "Timer.h"

using namespace AsyncFw;

class SingleTimerTask : public AbstractTask {
public:
  void init(const std::function<void()> &_f) { f = _f; }
  void invoke() override { f(); }
  std::function<void()> f;
};

void Timer::single(int ms, const std::function<void()> &f) {
  if (ms <= 0) {
    lsError() << "wrong timeout:" << ms;
    return;
  }
  AbstractThread *_t = AbstractThread::currentThread();
  SingleTimerTask *_s = new SingleTimerTask();
  int tid = _t->appendTimer(0, _s);
  _s->init([f, _t, tid]() {
    f();
    _t->removeTimer(tid);
  });
  _t->modifyTimer(tid, ms);
}

Timer::Timer() {
  thread_ = AbstractThread::currentThread();
  timerId = thread_->appendTimerTask(0, [this]() {
    timeout();
    if (single_) stop();
  });
  lsTrace();
}

Timer::~Timer() {
  thread_->removeTimer(timerId);
  lsTrace();
}

void Timer::start(int ms, bool single) {
  single_ = single;
  thread_->modifyTimer(timerId, ms);
}

void Timer::stop() { thread_->modifyTimer(timerId, 0); }
