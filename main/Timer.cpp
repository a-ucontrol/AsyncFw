/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "core/AbstractThread.h"
#include "core/LogStream.h"

#include "Timer.h"

using namespace AsyncFw;

Timer::Timer() {
  thread_ = AbstractThread::current();
  timerId = thread_->appendTimerTask(0, [this]() {
    if (single_) stop();
    timeout();
  });
  lsTrace();
}

Timer::~Timer() {
  thread_->removeTimer(timerId);
  destroying();
  lsTrace();
}

void Timer::start(int ms, bool single) {
  single_ = single;
  thread_->modifyTimer(timerId, ms);
}

void Timer::stop() { thread_->modifyTimer(timerId, 0); }

CoroutineAwait<void> Timer::coTimeout(int ms) {
  FunctionConnectionGuardList *_gl = new FunctionConnectionGuardList;
  return CoroutineAwait<void>([this, ms, _gl](CoroutineHandle h) {
    start(ms, true);
    *_gl += timeout.connect<AbstractFunctionConnector::Connection::Direct>([h, _gl]() {
      delete _gl;
      h.promise().resume_queued();
    });
    *_gl += destroying.connect<AbstractFunctionConnector::Connection::Direct>([h, _gl]() {
      delete _gl;
      h.destroy();
    });
  });
}

CoroutineAwait<void> AsyncFw::coTimeout(int ms) {
  return CoroutineAwait<void>([ms](CoroutineHandle h) { Timer::single(ms, [h]() { h.resume(); }); });
}

Timer::Single::Single(Single &&other) noexcept : t_(other.t_) {
  if (t_) {
    t_->st_ = this;
    other.t_ = nullptr;
  }
}

Timer::Single::~Single() {
  if (t_) t_->st_ = nullptr;
}

void Timer::Single::cancel() {
  if (!t_) return;
  if (t_->id_ >= 0) {
    t_->thread_->removeTimer(t_->id_);
    t_->id_ = -1;
  }
  t_->st_ = nullptr;
  t_ = nullptr;
}

Timer::Single &Timer::Single::operator=(Single &&other) noexcept {
  if (this != &other) {
    cancel();
    t_ = other.t_;
    if (t_) {
      t_->st_ = this;
      other.t_ = nullptr;
    }
  }
  return *this;
}
