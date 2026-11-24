/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Timer.h @brief The Timer class. */

#include "../core/FunctionConnector.h"
#include "Coroutine.h"

namespace AsyncFw {
/** @class Timer Timer.h <AsyncFw/Timer> @brief The Timer class
@par Example
@snippet Timer/main.cpp snippet */
class Timer {
  class Single {
  public:
    template <typename T>
    Single(int ms, T f) : t_(new SingleTask<T>(this, ms, std::move(f))) {}
    Single() = default;
    Single(const Single &) = delete;
    Single &operator=(const Single &) = delete;
    Single(Single &&other) noexcept;
    ~Single();
    Single &operator=(Single &&other) noexcept;
    void cancel();
    operator bool() const { return t_ != nullptr; }

  private:
    class AbstractSingleTask : public AbstractThread::AbstractTask {
      friend Single;

    protected:
      AbstractThread *thread_;
      Single *st_;
      int id_ = -1;
    };
    template <typename T>
    class SingleTask : public AbstractSingleTask {
    public:
      SingleTask(Single *st, int ms, T f) : f_(std::move(f)) {
        thread_ = AbstractThread::current();
        st_ = st;
        id_ = thread_->appendTimer(ms, this);
      }
      void operator()() override {
        if (id_ < 0) return;
        thread_->removeTimer(id_);
        id_ = -1;
        f_();
      }
      ~SingleTask() override {
        if (st_) st_->t_ = nullptr;
      }

    private:
      T f_;
    };
    AbstractSingleTask *t_ = nullptr;
  };

public:
  /** @class SingleGuard Timer.h <AsyncFw/Timer> @brief An RAII guard that automatically cancels a single-shot timer when destroyed or reassigned.
  @warning Not thread-safe. Must be owned and manipulated only by the thread that created the timer. */
  class SingleGuard : private Single {
  public:
    using Single::cancel;
    using Single::operator bool;
    using Single::operator=;
    SingleGuard() = default;
    ~SingleGuard() { cancel(); }
  };
  /** @brief Starts a single-shot timer with the specified timeout. @param ms Timeout interval in milliseconds. @param function Callable executed when the timer expires. @return A Single handle that can be used to cancel the timer or assigned to a SingleGuard. */
  template <typename T>
  static Single single(int ms, T function) {
    return Single(ms, std::move(function));
  }
  Timer();
  ~Timer();
  /** @brief Starts or restarts a timer with the specified timeout. If the timer is already running, it will be restarted @param ms timeout interval in milliseconds @param single if true, the timer will be a single shot */
  void start(int, bool = false);
  /** @brief Stops the timer */
  void stop();

  /** @brief The Timer::timeout connector */
  FunctionConnector<>::Protected<Timer> timeout;
  /** @brief The Timer::destroying connector */
  FunctionConnector<>::Protected<Timer> destroying;
  /** @brief Asynchronously waits for the specified timeout interval @param ms interval in milliseconds @return CoroutineAwait object to be used with co_await
  @par Example \code co_await timer.coTimeout(500); // Sleep for 500 milliseconds \endcode */
  CoroutineAwait<void> coTimeout(int);

private:
  AbstractThread *thread_;
  int timerId = -1;
  bool single_;
};
/** @brief Non-blocking coroutine sleep for the specified timeout interval. @param ms Timeout interval in milliseconds. @return CoroutineAwait object to be used with co_await.
@details Helper function to pause the current coroutine execution on the spot without creating or managing an explicit AsyncFw::Timer object instance.
\ingroup coroutine_api
@par Example \code co_await AsyncFw::coTimeout(500); // Sleep for 500 milliseconds without a Timer object \endcode */
CoroutineAwait<void> coTimeout(int);
}  // namespace AsyncFw
