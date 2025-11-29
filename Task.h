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
  virtual bool isRunning() = 0;
};

template <typename M>
class Task : public AbstractTask {
  friend class AbstractTask;
  class run {
    friend class Task;
    run(std::atomic_bool *ptr) : r_(ptr) {}
    std::atomic_bool *r_;

  public:
    ~run() { *r_ = false; }
  };

public:
  void invoke() override {
    running = true;
    if (!thread) {
      method(&data_);
      running = false;
      return;
    }
    std::shared_ptr<run> r = std::shared_ptr<run>(new run(&running));
    thread->invokeMethod([this, r]() { method(&data_); });
  }
  bool isRunning() override { return running; }
  Task(M method, AbstractThread *thread = nullptr) : method(method), thread(thread) {}

private:
  M method;
  AbstractThread *thread;
  std::atomic_bool running = false;
};
}  // namespace AsyncFw
