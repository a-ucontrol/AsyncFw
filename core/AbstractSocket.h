/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <cstdint>
#include <string>
#include "AnyData.h"

namespace AsyncFw {
class Thread;
class DataArray;
class LogStream;

/*! \class AbstractSocket AbstractSocket.h <AsyncFw/AbstractSocket> \brief The AbstractSocket class provides the base functionality for socket.
  \brief Example: \snippet Socket/main.cpp snippet */
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

  /*! \brief Listen for incoming connections on address and port. \param address address \param port port \return True if success */
  bool listen(const std::string &, uint16_t);

  DataArray &peek();
  int read(uint8_t *, int);
  DataArray read(int = 0);
  int write(const uint8_t *, int);
  int write(const DataArray &);

  Error error() const;
  std::string errorString() const;

  /*! \brief Return Thread that manages the socket. */
  Thread *thread() const { return thread_; }

  /*! \brief Return address. */
  std::string address() const;
  /*! \brief Return port. */
  uint16_t port() const;
  /*! \brief Return peer address. */
  std::string peerAddress() const;
  /*! \brief Return peer port. */
  uint16_t peerPort() const;

protected:
  AbstractSocket();
  AbstractSocket(int, int, int);
  virtual ~AbstractSocket();

  /*! \brief Called when established connection. */
  virtual void activateEvent();
  /*! \brief Called when the connection state changes. */
  virtual void stateEvent() {}
  /*! \brief Called when there is data to read. */
  virtual void readEvent() {}
  /*! \brief Called when writing to the socket is possible. */
  virtual void writeEvent() {}
  /*! \brief Called upon incoming connection. */
  virtual void incomingEvent() {}

  virtual int read_available_fd() const;
  virtual int read_fd(void *_p, int _s);
  virtual int write_fd(const void *_p, int _s);

  /*! \brief Returns the number of bytes of data pending to be read. */
  int pendingRead() const;
  /*! \brief Returns the number of bytes of data pending to be write. */
  int pendingWrite() const;

  void setError(Error);
  void setErrorString(const std::string &) const;

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
}  // namespace AsyncFw
