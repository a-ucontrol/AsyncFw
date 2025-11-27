#include "core/LogStream.h"
#include "PollNotifier.h"

using namespace AsyncFw;

PollNotifier::PollNotifier() {
  thread_ = AbstractThread::currentThread();
  lsTrace();
}

PollNotifier::PollNotifier(int fd, AbstractThread::PollEvents e) : PollNotifier() { setDescriptor(fd, e); }

PollNotifier::~PollNotifier() {
  if (fd_ != -1) removeDescriptor();
  lsTrace();
}

bool PollNotifier::setDescriptor(int fd, AbstractThread::PollEvents e) {
  bool _r = thread_->appendPollTask(fd, e, [this](AbstractThread::PollEvents e) {
    if (e & ~(AbstractThread::PollIn | AbstractThread::PollOut)) {
      lsError() << "descriptor:" << fd_ << ", event:" << e;
      thread_->removePollDescriptor(fd_);
      fail_ = true;
    }
    notify(e);
  });
  return !(fail_ = (fd_ = (_r) ? fd : -1) == -1);
}

bool PollNotifier::setEvents(AbstractThread::PollEvents e) { return !(fail_ = !thread_->modifyPollDescriptor(fd_, e)); }

void PollNotifier::removeDescriptor() {
  thread_->removePollDescriptor(fd_);
  fd_ = -1;
  fail_ = false;
}
