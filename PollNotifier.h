/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

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
