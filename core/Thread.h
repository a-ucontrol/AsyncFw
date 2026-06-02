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

  /** @brief The Thread::started connector */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> started;
  /** @brief The Thread::finished connector */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> finished;
  /** @brief The Thread::destroing connector */
  FunctionConnector<>::Policy<AbstractFunctionConnector::Direct>::Protected<Thread> destroing;

protected:
  /** @brief Runs started() */
  void startedEvent() override;
  /** @brief Runs finished() */
  void finishedEvent() override;

  std::vector<AbstractSocket *> sockets_;

private:
  struct Compare {
    bool operator()(const AbstractSocket *, const AbstractSocket *) const;
  };
};
}  // namespace AsyncFw
