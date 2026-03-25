/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <coroutine>
#include "core/AnyData.h"
#include "core/abstract_function.hpp"

namespace AsyncFw {
struct CoroutineAwait;

/*! \brief The CoroutineTask struct. */
struct CoroutineTask {
  CoroutineTask();
  virtual ~CoroutineTask();
  /*! \brief The CoroutineTask::promise_type struct. */
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
    /*! \brief Execute coroutine_handle::resume on the thread where the promise_type is created. */
    void resume_queued();

  private:
    Private *private_;
  };
  /*! \brief Return true if task finished. */
  bool finished();
  /*! \brief Runs nested Thread::exec() and wait for task finished. */
  void wait();

private:
  promise_type *promise;
};

using CoroutineHandle = std::coroutine_handle<CoroutineTask::promise_type>;

/*! \brief The CoroutineAwait struct. */
struct CoroutineAwait {
  template <typename T>
  CoroutineAwait(T _f) : f_(new Function(_f)) {}
  CoroutineAwait() = default;
  CoroutineAwait(const CoroutineAwait &) = delete;
  CoroutineAwait &operator=(const CoroutineAwait &) = delete;
  virtual ~CoroutineAwait();
  virtual void await_suspend(CoroutineHandle) const noexcept;
  virtual bool await_ready() const noexcept;
  virtual CoroutineHandle await_resume() const noexcept;

private:
  template <typename T>
  struct Function : AbstractFunction<const CoroutineHandle> {
    Function(T &_f) : f(std::move(_f)) {}
    void operator()(const CoroutineHandle _h) override { f(_h); }
    T f;
  };
  mutable CoroutineHandle h_;
  AbstractFunction<const CoroutineHandle> *f_ = nullptr;
};
}  // namespace AsyncFw
