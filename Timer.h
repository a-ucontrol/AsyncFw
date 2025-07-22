#pragma once

#include "core/FunctionConnector.h"

namespace AsyncFw {
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
