/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file AbstractSocket.h @brief The AbstractSocket class. */

#include <cstdint>
#include <string>
#include "AnyData.h"

namespace AsyncFw {
class Thread;
class DataArray;
class LogStream;

/** @class AbstractSocket AbstractSocket.h <AsyncFw/AbstractSocket> @brief Abstract base class providing core network socket functionality and OS descriptor abstraction.
@details AbstractSocket wraps native operating system network handles into a clean C++ interface. It handles non-blocking socket initialization, address binding, option configuration, and integrates directly into the AbstractThread I/O multiplexing event loop (epoll / poll).
@brief Example: @snippet Socket/main.cpp snippet */
class AbstractSocket : public AnyData {
  friend Thread;
  friend LogStream &operator<<(LogStream &, const AbstractSocket &);

public:
  /** @brief Execution and lifecycle states of the socket. */
  enum State : uint8_t {
    Unconnected,  ///< Initial state, no connection active.
    Listening,    ///< Server mode: listening for incoming connections.
    Connecting,   ///< Client connection handshake in progress.
    Connected,    ///< TCP layer connected (TLS handshake starts here if applicable).
    Active,       ///< Fully functional data transfer state.
    Closing,      ///< Connection closing in progress.
    Destroy       ///< Final destruction phase. WARNING: socket thread is nullptr during stateEvent().
  };

  enum Error : uint8_t { None, Closed, Refused, Read, Write, Activate };

  virtual void setDescriptor(int);
  /** @brief Initiates an asynchronous connection to a remote host. @param ip Remote target IPv4 or IPv6 address string. @param port Remote target port number. @return True if connection initiation succeeded. */
  virtual bool connect(const std::string &, uint16_t);
  virtual void disconnect();
  /** @brief Closes the network connection and unsubscribes from thread polling events. */
  virtual void close();
  /** @brief Schedules the socket for deferred asynchronous destruction. */
  virtual void destroy();

  /** @brief Synchronously detaches the socket from its execution thread.
  \note Sets socket thread to nullptr. Not thread-safe. */
  void removeFromThread();

  /** @brief Listen for incoming connections on address and port. @param address Address. @param port Port @return True if success. */
  bool listen(const std::string &, uint16_t);

  DataArray &peek();
  int read(uint8_t *, int);
  DataArray read(int = 0);
  int write(const uint8_t *, int);
  int write(const DataArray &);

  Error error() const;
  std::string errorString() const;

  /** @brief Return Thread that manages the socket. */
  Thread *thread() const { return thread_; }

  /** @brief Return address. */
  std::string address() const;
  /** @brief Return port. */
  uint16_t port() const;
  /** @brief Return peer address. */
  std::string peerAddress() const;
  /** @brief Return peer port. */
  uint16_t peerPort() const;

protected:
  /** @brief Constructs an AbstractSocket with default parameters (AF_INET, SOCK_STREAM, IPPROTO_TCP) and binds it to the current thread context. */
  AbstractSocket();
  /** @brief Constructs an AbstractSocket and binds it to the current thread context.
  @param family Address family @param type Socket type @param protocol Internet protocol */
  AbstractSocket(int, int, int);
  /** @brief Virtual destructor. Automatically detaches socket from thread if still attached. */
  virtual ~AbstractSocket();

  /** @brief Called when established connection. */
  virtual void activateEvent();
  /** @brief Called when the connection state changes.
  \warning When state is State::Destroy, the socket thread pointer is guaranteed to be nullptr. */
  virtual void stateEvent() {}
  /** @brief Called when there is data to read. */
  virtual void readEvent() {}
  /** @brief Called when writing to the socket is possible. */
  virtual void writeEvent() {}
  /** @brief Called upon incoming connection. */
  virtual void incomingEvent() {}

  virtual int read_available_fd() const;
  virtual int read_fd(void *_p, int _s);
  virtual int write_fd(const void *_p, int _s);

  /** @brief Returns the number of bytes of data pending to be read. */
  int pendingRead() const;
  /** @brief Returns the number of bytes of data pending to be write. */
  int pendingWrite() const;

  void setError(Error);
  void setErrorString(const std::string &) const;

  Thread *thread_;                    ///< Pointer to the execution thread managing this socket.
  int fd_ = -1;                       ///< Native operating system socket file descriptor.
  State state_ = State::Unconnected;  ///< Current tracking state of this socket.

private:
  AbstractSocket(const AbstractSocket &) = delete;
  AbstractSocket &operator=(const AbstractSocket &) = delete;
  void pollEvent(int);
  void changeDescriptor(int);
  void read_fd();
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
