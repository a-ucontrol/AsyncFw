#pragma once

#include <coroutine>
#include <functional>
#include "core/AnyData.h"

namespace AsyncFw {
struct CoroutineAwait;

struct CoroutineTask {
  CoroutineTask();
  virtual ~CoroutineTask();
  struct promise_type : public AnyData {
    friend CoroutineAwait;
    friend CoroutineTask;
    struct Private;
    promise_type();
    ~promise_type();
    CoroutineTask get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void unhandled_exception() {}
    void return_void();
    void resume_queued();

  private:
    Private *private_;
  };
  void wait();
  void resume();
  void resume_queued();
  bool finished();
  CoroutineAwait &await();

private:
  promise_type *promise;
};

struct CoroutineAwait : public AnyData {
  CoroutineAwait(std::function<void(std::coroutine_handle<CoroutineTask::promise_type>)> = nullptr);
  virtual ~CoroutineAwait();
  virtual void await_suspend(std::coroutine_handle<CoroutineTask::promise_type>) const noexcept;
  virtual bool await_ready() const noexcept;
  virtual std::coroutine_handle<CoroutineTask::promise_type> await_resume() const noexcept;

protected:
  mutable std::coroutine_handle<CoroutineTask::promise_type> h_;

private:
  std::function<void(std::coroutine_handle<CoroutineTask::promise_type>)> f_;
};
}  // namespace AsyncFw
