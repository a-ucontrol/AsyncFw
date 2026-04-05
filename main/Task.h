/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <memory>
#include "../core/AbstractThread.h"
#include "../core/AnyData.h"

namespace AsyncFw {
/*! \class AbstractTask Task.h <AsyncFw/Task> \brief The AbstractTask class. */
class AbstractTask : public AsyncFw::AbstractThread::AbstractTask, public AnyData {
public:
  AbstractTask();
  ~AbstractTask();
  virtual bool running() = 0;
};

/*! \class Task Task.h <AsyncFw/Task> \brief The Task class. */
template <typename M>
class Task : public AbstractTask {
public:
  Task(M &&method, AbstractThread *thread = nullptr) : method(std::move(method)), thread_(thread) {}
  void operator()() override {
    running_ = true;
    if (!thread_) {
      method(&data_);
      running_ = false;
      return;
    }
    thread_->invokeMethod([_m = std::move(method), _r = std::make_shared<std::atomic_bool>(&running_), _d = std::make_shared<std::any>(data_)]() {
      _m(_d.get());
      *_r = false;
    });
  }
  bool running() override { return running_; }

private:
  M method;
  AbstractThread *thread_;
  std::atomic_bool running_;
};
}  // namespace AsyncFw
