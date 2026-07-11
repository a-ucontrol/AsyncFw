/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Thread.h @brief The Thread class. */

#include "FunctionConnector.h"

namespace AsyncFw {
class AbstractSocket;

/** @class Thread Thread.h <AsyncFw/Thread> @brief AsyncFw::Thread thread with sockets. */
class Thread : public AbstractThread {
  friend AbstractSocket;
  friend class ListenSocket;
  friend LogStream &operator<<(LogStream &, const Thread &);

public:
  /** @brief Returns a pointer to the AsyncFw::Thread that manages the currently executing thread. */
  static Thread *current();

  /** @brief Constructs a thread. @param name thread name */
  Thread(const std::string & = "Thread");
  ~Thread() override;

  /** @brief Signal emitted within the newly spawned thread context immediately after its event loop starts.
  @note Defaults to a Direct connection policy. Subscribers execute instantly inside the target thread frame unless explicitly overridden. */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> started;
  /** @brief Signal emitted within the thread context just before its execution loop terminates.
  @note Defaults to a Direct connection policy. Ideal for final worker-specific logic execution. */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> finished;
  /** @brief Signal emitted inside the destructor framework immediately before the thread object resources are wiped.
  @warning Defaults to a Direct connection policy. If explicitly overridden to an asynchronous or Queued connection, ensure the subscriber lambda does NOT capture or reference the this pointer of the dying Thread instance to avoid use-after-free conditions. */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> destroying;

protected:
  /** @brief Runs started() */
  void startedEvent() override;
  /** @brief Runs finished() */
  void finishedEvent() override;

  /** @brief Internal collection of non-blocking network sockets assigned to this thread.
  @warning This vector is strictly ordered by their native operating system descriptors to ensure binary-search lookup speeds. Never modify this array directly. */
  std::vector<AbstractSocket *> sockets_;

private:
  struct Compare {
    bool operator()(const AbstractSocket *, const AbstractSocket *) const;
  };
};
}  // namespace AsyncFw
