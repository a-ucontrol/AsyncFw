/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \example Coroutine/main.cpp Coroutine example \example CoroutineWait/main.cpp CoroutineWait example \example CoroutineInvoke/main.cpp CoroutineInvoke example */

#include <coroutine>
#include "../core/AnyData.h"
#include "ThreadPool.h"

namespace AsyncFw {
/*! \struct CoroutineTask Coroutine.h <AsyncFw/Coroutine> \brief A class representing a coroutine task that can be suspended and resumed.
\brief It manages the C++20 coroutine state, holds the underlying handle, and orchestrates the lifecycle via its internal promise_type. */
struct CoroutineTask {
  CoroutineTask();
  virtual ~CoroutineTask();
  /*! \struct promise_type Coroutine.h <AsyncFw/Coroutine> \brief The CoroutineTask::promise_type struct. */
  struct promise_type : public AnyData {
    friend CoroutineTask;
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
    struct Private;
    Private &private_;
  };
  /*! \brief Return true if task finished. */
  bool finished();
  /*! \brief Runs nested Thread::exec() and wait for task finished. */
  void wait();

private:
  promise_type *promise;
};

using CoroutineHandle = std::coroutine_handle<CoroutineTask::promise_type>;

/*! \struct CoroutineAwait Coroutine.h <AsyncFw/Coroutine> \brief The CoroutineAwait struct. */
template <typename R>
struct CoroutineAwait {
  template <typename T>
  CoroutineAwait(T f) : f_(new Invocable<void(const CoroutineHandle)>::Function(std::forward<T>(f))) {}
  CoroutineAwait() = default;
  CoroutineAwait(const CoroutineAwait &) = delete;
  CoroutineAwait &operator=(const CoroutineAwait &) = delete;
  virtual ~CoroutineAwait() {
    if (f_) delete f_;
  }
  virtual void await_suspend(CoroutineHandle h) const noexcept {
    h_ = h;
    if (f_) (*f_)(h);
  }
  virtual bool await_ready() const noexcept { return false; }
  virtual R await_resume() const noexcept {
    if constexpr (!std::is_void_v<R>) return h_.promise().data<R>();
  }

private:
  mutable CoroutineHandle h_;
  Invocable<void(const CoroutineHandle)>::Abstract *f_ = nullptr;
};

template <typename T>
struct CoroutineInvokeAwait;
template <typename R, typename... Args>
struct CoroutineInvokeAwait<R(Args...)> {
  template <typename F, typename... A>
  CoroutineInvokeAwait(AbstractThread *thread, F &&f, A &&...a) : t_(thread), f_(new Invocable<R(Args...)>::template Function<F>(std::forward<F>(f))), a_(std::forward_as_tuple(std::forward<A>(a)...)) {}
  template <typename M, typename O, typename... A>
  CoroutineInvokeAwait(AbstractThread *thread, M m, O *o, A &&...a) : t_(thread), f_(new Invocable<R(Args...)>::template MemberFunction<M, O>(m, o)), a_(std::forward_as_tuple(std::forward<A>(a)...)) {}
  ~CoroutineInvokeAwait() { delete f_; }

  void await_suspend(CoroutineHandle h) const noexcept {
    h_ = h;
    ThreadPool::async(t_ ? t_ : ThreadPool::instance()->getThread(), [this]() {
      if constexpr (std::is_void_v<R>) std::apply(*f_, a_);
      else { h_.promise().setData(std::apply(*f_, a_)); }
      h_.promise().resume_queued();
    });
  }
  bool await_ready() const noexcept { return false; }
  R await_resume() const noexcept {
    if constexpr (!std::is_void_v<R>) return h_.promise().data<R>();
  }

private:
  AbstractThread *t_;
  mutable CoroutineHandle h_;
  mutable std::tuple<Args...> a_;
  Invocable<R(Args...)>::Abstract *f_;
};

template <typename F, typename... Args>
auto coInvoke(F &&f, Args &&...args) {
  return CoroutineInvokeAwait<std::invoke_result_t<F, Args &...>(Args & ...)>(nullptr, std::forward<F>(f), std::forward<Args>(args)...);
}
template <typename T = AbstractThread, typename F, typename... Args>
auto coInvoke(T *t, F &&f, Args &&...args) {
  return CoroutineInvokeAwait<std::invoke_result_t<F, Args &...>(Args & ...)>(t, std::forward<F>(f), std::forward<Args>(args)...);
}
template <typename M, typename O, typename... Args>
auto coInvoke(M m, O *o, Args &&...args) {
  return CoroutineInvokeAwait<std::invoke_result_t<M, O, Args &...>(Args & ...)>(nullptr, m, o, std::forward<Args>(args)...);
}
template <typename T = AbstractThread, typename M, typename O, typename... Args>
auto coInvoke(T *t, M m, O *o, Args &&...args) {
  return CoroutineInvokeAwait<std::invoke_result_t<M, O, Args &...>(Args & ...)>(t, m, o, std::forward<Args>(args)...);
}
}  // namespace AsyncFw
