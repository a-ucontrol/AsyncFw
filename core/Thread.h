/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "FunctionConnector.h"

namespace AsyncFw {
class AbstractSocket;

/*! \brief AsyncFw::Thread thread with sockets. */
class Thread : public AbstractThread {
  friend AbstractSocket;
  friend class ListenSocket;
  friend LogStream &operator<<(LogStream &, const Thread &);

public:
  /*! \brief Returns a pointer to the AsyncFw::Thread that manages the currently executing thread. */
  static Thread *currentThread();
  Thread(const std::string & = "Thread");
  ~Thread() override;

  FunctionConnectorProtected<Thread>::Connector<> started;
  FunctionConnectorProtected<Thread>::Connector<> finished;
  FunctionConnectorProtected<Thread>::Connector<> destroing;

protected:
  std::vector<AbstractSocket *> sockets_;
  void startedEvent() override;
  void finishedEvent() override;

private:
  struct Compare {
    bool operator()(const AbstractSocket *, const AbstractSocket *) const;
  };
  void appendSocket(AbstractSocket *);
  void removeSocket(AbstractSocket *);
};
}  // namespace AsyncFw
