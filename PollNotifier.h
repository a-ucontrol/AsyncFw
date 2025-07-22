#pragma once

#include "core/Thread.h"
#include "core/FunctionConnector.h"

namespace AsyncFw {
class PollNotifier {
public:
  PollNotifier();
  PollNotifier(int, AbstractThread::PollEvents = AbstractThread::PollIn);
  ~PollNotifier();
  bool setDescriptor(int, AbstractThread::PollEvents = AbstractThread::PollIn);
  bool setEvents(AbstractThread::PollEvents);
  void removeDescriptor();
  int descriptor() { return fd_; }
  bool fail() { return fail_; }

  FunctionConnectorProtected<PollNotifier>::Connector<AbstractThread::PollEvents> notify;

private:
  AbstractThread *thread_;
  int fd_ = -1;
  bool fail_ = false;
};
}  // namespace AsyncFw
