/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Task.h @brief The AbstractTask and Task classes. */
/** @example ContainerTask/main.cpp ContainerTask example */

#include <memory>
#include "../core/AbstractThread.h"
#include "../core/AnyData.h"

namespace AsyncFw {
/** @class AbstractTask Task.h <AsyncFw/Task> @brief Base polymorphic class for tasks with dynamic runtime data encapsulation.
@details Integrates event-loop execution tracking, custom context thread binding, and polymorphically managed storage via AsyncFw::AnyData inheritance. */
class AbstractTask : public AsyncFw::AbstractThread::AbstractTask, public AnyData, public std::enable_shared_from_this<AbstractTask> {
public:
  /** @brief Constructs a task shell bound to an optional execution context thread. */
  AbstractTask(AbstractThread * = nullptr);
  /** @brief Non-blocking destructor. Execution cleanup is handled asynchronously. */
  virtual ~AbstractTask();
  /** @brief Returns true if the task payload is currently processing in a thread loop. */
  bool running();

protected:
  std::atomic_bool running_;
  AbstractThread *thread_;
};

/** @class Task Task.h <AsyncFw/Task> @brief A template container to execute custom callable payloads with dynamic data binding.
@details Accepts data updates at runtime via standard setData() before execution. Safely manages object lifetime across worker threads using C++20 lambda capture.
@tparam F The unique type of the callable function or lambda expression. */
template <typename F>
class Task : public AbstractTask {
public:
  /** @brief Constructs a task by moving the custom callable function payload. */
  Task(F &&function, AbstractThread *thread = nullptr) : AbstractTask(thread), function(std::move(function)) {}
  /** @brief Invokes the internal callable function, safely passing a pointer to the encapsulated AnyData. */
  void operator()() override {
    running_ = true;
    if (!thread_) {
      function(&data_);
      running_ = false;
      return;
    }
    thread_->invoke([_p = shared_from_this()]() {
      Task<F> *_task = static_cast<Task<F> *>(_p.get());
      (_task->function)(&_task->data_);
      _task->running_ = false;
    });
  }

private:
  F function;
};
}  // namespace AsyncFw
