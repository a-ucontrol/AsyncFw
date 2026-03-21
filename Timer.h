/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "core/FunctionConnector.h"

namespace AsyncFw {
/*! \brief The Timer class
 \snippet Timer/main.cpp snippet */
class Timer {
public:
  template <typename T>
  static void single(int ms, T f) {
    new SingleTimerTask(ms, f);
  }

  Timer();
  ~Timer();
  void start(int, bool = false);
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
