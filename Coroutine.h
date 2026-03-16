/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

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

private:
  promise_type *promise;
};

using CoroutineHandle = std::coroutine_handle<CoroutineTask::promise_type>;

struct CoroutineAwait {
  CoroutineAwait(const std::function<void(CoroutineHandle)> & = nullptr);
  virtual ~CoroutineAwait();
  virtual void await_suspend(CoroutineHandle) const noexcept;
  virtual bool await_ready() const noexcept;
  virtual CoroutineHandle await_resume() const noexcept;

protected:
  mutable CoroutineHandle h_;

private:
  std::function<void(CoroutineHandle)> f_;
};
}  // namespace AsyncFw
