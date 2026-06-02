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
  lsTrace();
}

void Timer::start(int ms, bool single) {
  single_ = single;
  thread_->modifyTimer(timerId, ms);
}

void Timer::stop() { thread_->modifyTimer(timerId, 0); }

CoroutineAwait<void> Timer::coTimeout(int ms) {
  FunctionConnectionGuard *_g = new FunctionConnectionGuard;
  return CoroutineAwait<void>([this, ms, _g](CoroutineHandle h) {
    start(ms, true);
    *_g = timeout.connect<AbstractFunctionConnector::Connection::Queued>([h, _g]() {
      delete _g;
      h.resume();
    });
  });
}

CoroutineAwait<void> AsyncFw::coTimeout(int ms) {
  return CoroutineAwait<void>([ms](CoroutineHandle h) { Timer::single(ms, [h]() { h.promise().resume_queued(); }); });
}
