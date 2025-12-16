#pragma once

#include "core/Thread.h"
#include "core/AnyData.h"

namespace AsyncFw {
class AbstractTask : public AnyData {
public:
  AbstractTask();
  virtual ~AbstractTask();
  virtual void invoke() = 0;
  virtual bool running() = 0;
};

template <typename M>
class Task : public AbstractTask {
public:
  Task(M &&method, AbstractThread *thread = nullptr) : method(std::move(method)), thread(thread) {}
  void invoke() override {
    running_ = true;
    if (!thread) {
      method(&data_);
      running_ = false;
      return;
    }
    thread->invokeMethod([_m = std::move(method), _r = std::make_shared<std::atomic_bool>(&running_), _d = std::make_shared<std::any>(data_)]() {
      _m(_d.get());
      *_r = false;
    });
  }
  bool running() override { return running_; }

private:
  M method;
  AbstractThread *thread;
  std::atomic_bool running_;
};
}  // namespace AsyncFw
