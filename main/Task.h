/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \example ContainerTask/main.cpp ContainerTask example */

#include "../core/AbstractThread.h"
#include "../core/AnyData.h"

namespace AsyncFw {
/*! \class AbstractTask Task.h <AsyncFw/Task> \brief The AbstractTask class. */
class AbstractTask : public AsyncFw::AbstractThread::AbstractTask, public AnyData {
public:
  AbstractTask(AbstractThread * = nullptr);
  virtual ~AbstractTask();
  bool running();

protected:
  std::atomic_bool running_;
  AbstractThread *thread_;
};

/*! \class Task Task.h <AsyncFw/Task> \brief The Task class. */
template <typename F>
class Task : public AbstractTask {
public:
  Task(F &&function, AbstractThread *thread = nullptr) : AbstractTask(thread), function(std::move(function)) {}
  void operator()() override {
    running_ = true;
    if (!thread_) {
      function(&data_);
      running_ = false;
      return;
    }
    thread_->invoke([this]() {
      function(&data_);
      running_ = false;
    });
  }

private:
  F function;
};
}  // namespace AsyncFw
