/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "core/FunctionConnector.h"

namespace AsyncFw {
/*! \brief The Timer class
 \brief Example: \snippet Timer/main.cpp snippet */
class Timer {
public:
  /*! \brief Starts a single shot timer with the specified timeout. \param ms timeout interval in milliseconds \param function runs at timeout */
  template <typename T>
  static void single(int ms, T function) {
    new SingleTimerTask(ms, function);
  }

  Timer();
  ~Timer();
  /*! \brief Starts or restarts a timer with the specified timeout. If the timer is already running, it will be restarted. \param ms timeout interval in milliseconds \param single if true, the timer will be a single shot */
  void start(int, bool = false);
  /*! \brief Stops the timer */
  void stop();

  /*! \brief The Timer::timeout connector */
  FunctionConnectorProtected<Timer>::Connector<> timeout;

private:
  template <typename T>
  class SingleTimerTask : public AbstractThread::AbstractTask {
  public:
    SingleTimerTask(int ms, T _f) : f(std::move(_f)) {
      t = AbstractThread::currentThread();
      id = t->appendTimer(ms, this);
    }
    void invoke() override {
      t->removeTimer(id);
      f();
    }

  private:
    AbstractThread *t;
    int id;
    T f;
  };
  AbstractThread *thread_;
  int timerId = -1;
  bool single_;
};
}  // namespace AsyncFw
