/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "core/AbstractThread.h"
#include "core/LogStream.h"

#include "Coroutine.h"

using namespace AsyncFw;

struct CoroutineTask::promise_type::Private {
  CoroutineTask *task;
  AbstractThread *thread;
  AbstractThread::Waiter waiter;
  bool finished = false;
};

CoroutineTask::CoroutineTask() { lsTrace(); }

CoroutineTask::~CoroutineTask() {
  std::coroutine_handle<promise_type> h = std::coroutine_handle<promise_type>::from_promise(*promise);
  if (h.promise().private_.finished) h.destroy();
  else { h.promise().private_.task = nullptr; }
  lsTrace();
}

void CoroutineTask::wait() {
  if (promise->private_.finished) return;
  promise->private_.waiter.wait();
}

bool CoroutineTask::finished() { return promise->private_.finished; }

CoroutineTask::promise_type::promise_type() : private_(*new Private) {
  private_.thread = AbstractThread::current();
  lsTrace();
}

CoroutineTask::promise_type::~promise_type() {
  delete &private_;
  lsTrace();
}

CoroutineTask CoroutineTask::promise_type::get_return_object() {
  CoroutineTask t;
  t.promise = this;
  private_.task = &t;
  return t;
}

std::suspend_never CoroutineTask::promise_type::initial_suspend() noexcept { return {}; }

std::suspend_always CoroutineTask::promise_type::final_suspend() noexcept {
  if (!private_.task) private_.thread->invoke([this]() { std::coroutine_handle<promise_type>::from_promise(*this).destroy(); });
  return {};
}

void CoroutineTask::promise_type::return_void() {
  if (private_.waiter.waiting()) private_.waiter.complete();
  private_.finished = true;
}

void CoroutineTask::promise_type::resume_queued() {
  private_.thread->invoke([this]() { std::coroutine_handle<promise_type>::from_promise(*this).resume(); });
}
