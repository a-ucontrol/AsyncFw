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
@brief Example: @snippet Timer/main.cpp snippet */
class Timer {
public:
  /** @brief Starts a single shot timer with the specified timeout @param ms timeout interval in milliseconds @param function runs at timeout */
  template <typename T>
  static void single(int ms, T function) {
    new SingleTimerTask(ms, function);
  }

  Timer();
  ~Timer();
  /** @brief Starts or restarts a timer with the specified timeout. If the timer is already running, it will be restarted @param ms timeout interval in milliseconds @param single if true, the timer will be a single shot */
  void start(int, bool = false);
  /** @brief Stops the timer */
  void stop();

  /** @brief The Timer::timeout connector */
  FunctionConnector<>::Protected<Timer> timeout;
  /** @brief Asynchronously waits for the specified timeout interval @param ms interval in milliseconds @return CoroutineAwait object to be used with co_await
  @brief Example: \code co_await timer.coTimeout(500); // Sleep for 500 milliseconds \endcode */
  CoroutineAwait<void> coTimeout(int);

private:
  template <typename T>
  class SingleTimerTask : public AbstractThread::AbstractTask {
  public:
    SingleTimerTask(int ms, T f) : f_(std::move(f)) {
      thread_ = AbstractThread::current();
      id_ = thread_->appendTimer(ms, this);
    }
    void operator()() override {
      thread_->removeTimer(id_);
      f_();
    }

  private:
    AbstractThread *thread_;
    int id_;
    T f_;
  };
  AbstractThread *thread_;
  int timerId = -1;
  bool single_;
};
/** @brief Non-blocking coroutine sleep for the specified timeout interval. @param ms Timeout interval in milliseconds. @return CoroutineAwait object to be used with co_await.
@details Helper function to pause the current coroutine execution on the spot without creating or managing an explicit AsyncFw::Timer object instance.
\ingroup coroutine_api
@brief Example: \code co_await AsyncFw::coTimeout(500); // Sleep for 500 milliseconds without a Timer object \endcode */
CoroutineAwait<void> coTimeout(int);
}  // namespace AsyncFw
