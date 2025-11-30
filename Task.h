#pragma once

#include "core/Thread.h"
#include "core/AnyData.h"

namespace AsyncFw {
template <typename M>
class Task;

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
  void invoke() override {
    running_ = true;
    if (!thread) {
      method(&data_);
      running_ = false;
      return;
    }
    thread->invokeMethod([_m = method, _r = std::make_shared<std::atomic_bool>(&running_), _d = std::make_shared<std::any>(data_)]() {
      _m(_d.get());
      *_r = false;
    });
  }
  bool running() override { return running_; }
  Task(M method, AbstractThread *thread = nullptr) : method(method), thread(thread) {}

private:
  M method;
  AbstractThread *thread;
  std::atomic_bool running_;
};
}  // namespace AsyncFw
