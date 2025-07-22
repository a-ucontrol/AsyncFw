#include "core/Thread.h"
#include "core/LogStream.h"

#include "Coroutine.h"

using namespace AsyncFw;

struct CoroutineTask::promise_type::Private {
  CoroutineAwait *await;
  CoroutineTask *task;
  AbstractThread *thread;
  ExecLoopThread::Holder *holder = nullptr;
  bool finished = false;
};

CoroutineAwait::~CoroutineAwait() { ucTrace(); }

CoroutineAwait::CoroutineAwait(std::function<void(std::coroutine_handle<CoroutineTask::promise_type>)> f) : f_(f) { ucTrace(); }

void CoroutineAwait::await_suspend(std::coroutine_handle<CoroutineTask::promise_type> h) const noexcept {
  h_ = h;
  h.promise().private_->await = const_cast<CoroutineAwait *>(this);
  if (f_) f_(h);
}

bool CoroutineAwait::await_ready() const noexcept { return false; }

std::coroutine_handle<CoroutineTask::promise_type> CoroutineAwait::await_resume() const noexcept { return h_; }

CoroutineTask::CoroutineTask() { ucTrace(); }

CoroutineTask::~CoroutineTask() {
  std::coroutine_handle<promise_type> h = std::coroutine_handle<promise_type>::from_promise(*promise);
  if (h.promise().private_->finished) h.destroy();
  else { h.promise().private_->task = nullptr; }
  ucTrace();
}

void CoroutineTask::wait() {
  if (promise->private_->finished) return;

  ExecLoopThread::Holder h;
  promise->private_->holder = &h;
  h.wait();

  promise->private_->holder = nullptr;
}

void CoroutineTask::resume() { std::coroutine_handle<promise_type>::from_promise(*promise).resume(); }

void CoroutineTask::resume_queued() { promise->resume_queued(); }

bool CoroutineTask::finished() { return promise->private_->finished; }

CoroutineAwait &CoroutineTask::await() { return *promise->private_->await; }

CoroutineTask::promise_type::promise_type() {
  private_ = new Private;
  private_->thread = AbstractThread::currentThread();
  ucTrace();
}

CoroutineTask::promise_type::~promise_type() {
  delete private_;
  ucTrace();
}

CoroutineTask CoroutineTask::promise_type::get_return_object() {
  CoroutineTask t;
  t.promise = this;
  private_->task = &t;
  return t;
}

std::suspend_never CoroutineTask::promise_type::initial_suspend() noexcept { return {}; }

std::suspend_always CoroutineTask::promise_type::final_suspend() noexcept {
  if (!private_->task) private_->thread->invokeMethod([this]() { std::coroutine_handle<promise_type>::from_promise(*this).destroy(); });
  return {};
}

void CoroutineTask::promise_type::return_void() {
  if (private_->holder) private_->holder->complete();
  private_->finished = true;
}

void CoroutineTask::promise_type::resume_queued() {
  private_->thread->invokeMethod([this]() { std::coroutine_handle<promise_type>::from_promise(*this).resume(); });
}
