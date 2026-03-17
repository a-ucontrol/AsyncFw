/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <functional>

#include "core/FunctionConnector.h"

namespace AsyncFw {
/*! \brief The Timer class
 \snippet Timer/main.cpp snippet
*/
class Timer {
public:
  static void single(int, const std::function<void()> &);

  Timer();
  ~Timer();
  void start(int, bool = false);
  void stop();

  FunctionConnectorProtected<Timer>::Connector<> timeout;

private:
  AbstractThread *thread_;
  int timerId = -1;
  bool single_;
};
}  // namespace AsyncFw
