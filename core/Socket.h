/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <cstdint>
#include <string>

#include "AnyData.h"
#include "FunctionConnector.h"

namespace AsyncFw {
class Thread;
class DataArray;
class LogStream;

/*! \brief The AbstractSocket class provides the base functionality for socket. */
class AbstractSocket : public AnyData {
  friend Thread;
  friend LogStream &operator<<(LogStream &, const AbstractSocket &);
  struct Private;

public:
  enum State : uint8_t { Unconnected, Listening, Connecting, Connected, Active, Closing, Destroy };
  enum Error : uint8_t { None, Closed, Refused, Read, Write, Activate };

  virtual void setDescriptor(int);
  virtual bool connect(const std::string &, uint16_t);
  virtual void disconnect();
  virtual void close();
  virtual void destroy();

  bool listen(const std::string &, uint16_t);

  DataArray &peek();
  int read(uint8_t *, int);
  DataArray read(int = 0);
  int write(const uint8_t *, int);
  int write(const DataArray &);

  Error error() const;
  std::string errorString() const;

  Thread *thread() const { return thread_; }

  std::string address() const;
  uint16_t port() const;
  std::string peerAddress() const;
  uint16_t peerPort() const;

protected:
  AbstractSocket();
  AbstractSocket(int, int, int);
  virtual ~AbstractSocket();

  int pendingRead() const;
  int pendingWrite() const;
  void setError(Error);
  void setErrorString(const std::string &) const;

  virtual int read_available_fd() const;
  virtual int read_fd(void *_p, int _s);
  virtual int write_fd(const void *_p, int _s);

  virtual void activateEvent();

  virtual void stateEvent() {}
  virtual void readEvent() {}
  virtual void writeEvent() {}
  virtual void incomingEvent() {}

  Thread *thread_;
  int fd_ = -1;
  mutable State state_ = State::Unconnected;

private:
  AbstractSocket(const AbstractSocket &) = delete;
  AbstractSocket &operator=(const AbstractSocket &) = delete;
  void pollEvent(int);
  void changeDescriptor(int);
  void read_fd();
  Private *private_;
};

/*! \brief The ListenSocket class. */
class ListenSocket : public AbstractSocket {
public:
  ~ListenSocket();
  /*! \brief The FunctionConnector for incoming connections. */
  AsyncFw::FunctionConnectorProtected<ListenSocket>::Connector<int, const std::string &, bool *> incoming {AsyncFw::AbstractFunctionConnector::SyncOnly};

protected:
  void incomingEvent() override;
};

}  // namespace AsyncFw
