/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "core/LogStream.h"
#include "PollNotifier.h"

using namespace AsyncFw;

PollNotifier::PollNotifier() {
  thread_ = AbstractThread::currentThread();
  lsTrace();
}

PollNotifier::PollNotifier(int fd, AbstractThread::PollEvents events) : PollNotifier() { setDescriptor(fd, events); }

PollNotifier::~PollNotifier() {
  if (fd_ != -1) removeDescriptor();
  lsTrace();
}

bool PollNotifier::setDescriptor(int fd, AbstractThread::PollEvents events) {
  bool _r = thread_->appendPollTask(fd, events, [this](AbstractThread::PollEvents events) {
    if (events & ~(AbstractThread::PollIn | AbstractThread::PollOut)) {
      lsError() << "descriptor:" << fd_ << ", event:" << events;
      thread_->removePollDescriptor(fd_);
      fail_ = true;
      return;
    }
    notify(events);
  });
  return !(fail_ = (fd_ = (_r) ? fd : -1) == -1);
}

bool PollNotifier::setEvents(AbstractThread::PollEvents events) { return !(fail_ = !thread_->modifyPollDescriptor(fd_, events)); }

void PollNotifier::removeDescriptor() {
  thread_->removePollDescriptor(fd_);
  fd_ = -1;
  fail_ = false;
}
