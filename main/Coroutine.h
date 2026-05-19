/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \example Coroutine/main.cpp Coroutine example \example CoroutineWait/main.cpp CoroutineWait example \example CoroutineFunctionAwait/main.cpp CoroutineFunctionAwait example */

#include <coroutine>
#include "../core/AnyData.h"
#include "ThreadPool.h"

namespace AsyncFw {
/*! \struct CoroutineTask Coroutine.h <AsyncFw/Coroutine> \brief The CoroutineTask struct. */
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
struct CoroutineAwait {
  template <typename T>
  CoroutineAwait(T f) : f_(new Invocable<const CoroutineHandle>::Function(std::forward<T>(f))) {}
  CoroutineAwait() = default;
  CoroutineAwait(const CoroutineAwait &) = delete;
  CoroutineAwait &operator=(const CoroutineAwait &) = delete;
  virtual ~CoroutineAwait();
  virtual void await_suspend(CoroutineHandle) const noexcept;
  virtual bool await_ready() const noexcept;
  virtual CoroutineHandle await_resume() const noexcept;

private:
  mutable CoroutineHandle h_;
  Invocable<const CoroutineHandle>::Abstract<void> *f_ = nullptr;
};

template <typename F, typename... Args>
struct CoroutineFunctionAwait {
  using R = typename std::invoke_result<F, Args...>::type;
  CoroutineFunctionAwait(AbstractThread *thread, F &&f, Args &&...a) : thread_(thread), f_(std::move(f)), t_(std::tuple(std::forward<Args>(a)...)) {}
  CoroutineFunctionAwait(AbstractThread *thread, F &&f, const Args &...a) : thread_(thread), f_(std::move(f)), t_(std::tuple(a...)) {}
  void await_suspend(CoroutineHandle h) const noexcept {
    h_ = h;
    ThreadPool::async(thread_ ? thread_ : ThreadPool::instance()->getThread(), [this]() {
      if constexpr (std::is_void<R>::value) std::apply(f_, t_);
      else {
        R _r = std::apply(f_, t_);
        h_.promise().setData(_r);
      }
      h_.promise().resume_queued();
    });
  }
  bool await_ready() const noexcept { return false; }
  R await_resume() const noexcept {
    if constexpr (!std::is_void<R>::value) return h_.promise().data<R>();
  }

private:
  AbstractThread *thread_;
  mutable CoroutineHandle h_;
  std::tuple<Args...> t_;
  F f_;
};

template <typename F, typename... Args>
CoroutineFunctionAwait<F, Args...> coFunction(F &&f, Args &&...args) {
  return CoroutineFunctionAwait {nullptr, std::forward<F>(f), std::forward<Args>(args)...};
}

template <typename T = AbstractThread, typename F, typename... Args>
CoroutineFunctionAwait<F, Args...> coFunction(T *t, F &&f, Args &&...args) {
  return CoroutineFunctionAwait {t, std::forward<F>(f), std::forward<Args>(args)...};
}

template <typename F, typename... Args>
CoroutineFunctionAwait<F, Args...> coFunction(F &&f, Args &...args) {
  return CoroutineFunctionAwait {nullptr, std::forward<F>(f), args...};
}

template <typename T = AbstractThread, typename F, typename... Args>
CoroutineFunctionAwait<F, Args...> coFunction(T *t, F &&f, Args &...args) {
  return CoroutineFunctionAwait {t, std::forward<F>(f), args...};
}
template <typename M, typename O, typename... Args>
auto coFunction(M m, O *o, const Args &&...args) {
  return CoroutineFunctionAwait {nullptr, [o, m](Args... args) { return (o->*m)(std::move(args)...); }, std::forward<Args>(args)...};
}

template <typename T = AbstractThread, typename M, typename O, typename... Args>
auto coFunction(T *t, M m, O *o, Args &&...args) {
  return CoroutineFunctionAwait {t, [o, m](Args... args) { return (o->*m)(std::move(args)...); }, std::forward<Args>(args)...};
}

template <typename M, typename O, typename... Args>
auto coFunction(M m, O *o, Args &...args) {
  return CoroutineFunctionAwait {nullptr, [o, m](Args... args) { return (o->*m)(std::move(args)...); }, args...};
}

template <typename T = AbstractThread, typename M, typename O, typename... Args>
auto coFunction(T *t, M m, O *o, Args &...args) {
  return CoroutineFunctionAwait {t, [o, m](Args... args) { return (o->*m)(std::move(args)...); }, args...};
}
}  // namespace AsyncFw
