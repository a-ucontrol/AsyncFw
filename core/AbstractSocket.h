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
  /** @brief Runtime error categories for network and I/O operations. */
  enum Error : uint8_t {
    None,     ///< No error occurred.
    Closed,   ///< Connection was closed by the remote peer.
    Refused,  ///< Connection attempt was refused by the host.
    Read,     ///< Low-level hardware or OS socket read error.
    Write,    ///< Low-level hardware or OS socket write error.
    Activate  ///< Protocol negotiation or handshake initialization error (e.g., TLS setup failure).
  };
  /** @brief Mode for interpreting data within the output buffer.
  @details This only makes sense for derived classes whose application data is not equivalent to network data. For example, encryption.
  @note Under normal conditions, this buffer is always empty. Data is accumulated here ONLY if it cannot be written to the socket immediately. */
  enum OutputBufferMode : uint8_t {
    Application = 0x01,  ///< Raw application data (requires encryption/processing before sending).
    Network = 0x02       ///< Ready-to-send network bytes (transport layer, written directly to socket).
  };

  /** @brief Binds a raw native operating system socket file descriptor to this instance. @param fd Native socket descriptor integer. */
  virtual void setDescriptor(int);
  /** @brief Initiates an asynchronous connection to a remote host. @param ip Remote target IPv4 or IPv6 address string. @param port Remote target port number. @return True if connection initiation succeeded. */
  virtual bool connect(const std::string &, uint16_t);
  /** @brief Disconnects the socket from the remote host gracefully. */
  virtual void disconnect();
  /** @brief Closes the network connection and unsubscribes from thread polling events. */
  virtual void close();
  /** @brief Schedules the socket for deferred asynchronous destruction. */
  virtual void destroy();

  /** @brief Synchronously detaches the socket from its execution thread.
  @note Sets socket thread to nullptr. Not thread-safe. */
  void removeFromThread();
  /** @brief Listen for incoming connections on address and port. @param address Address. @param port Port @return True if success. */
  bool listen(const std::string &, uint16_t);
  /** @brief Non-destructively inspects the internal unread data buffer without consuming it. @return Reference to a DataArray containing the currently buffered incoming data. */
  DataArray &peek();
  /** @brief Reads incoming data into a raw byte buffer up to a specified maximum size. @param buffer Destination raw byte array pointer. @param maxSize Maximum number of bytes to read into the buffer. @return Number of bytes successfully read, or a negative value on error. */
  int read(uint8_t *, int);
  /** @brief Reads a chunk of incoming data and extracts it into a returned DataArray object. @param size Exact or maximum chunk size to extract. If 0, extracts all available data. @return A DataArray containing the read payload. */
  DataArray read(int = 0);
  /** @brief Writes raw binary bytes out to the network socket layer. @param data Source raw memory buffer pointer containing data to transmit. @param size The number of bytes to transmit from the data buffer. @return Number of bytes successfully dispatched to the socket queue, or a negative value on error. */
  int write(const uint8_t *, int);
  /** @brief Transmits a structural DataArray package out to the network layer. @param data Reference to the DataArray containing the payload to write. @return Number of bytes successfully dispatched to the socket queue, or a negative value on error. */
  int write(const DataArray &);
  /** @brief Fetches the most recent runtime error category flag assigned to this socket. @return An Error enum value. */
  Error error() const;
  /** @brief Returns a human-readable text string describing the last runtime socket error. @return Error message string. */
  std::string errorString() const;
  /** @brief Return Thread that manages the socket. */
  Thread *thread() const { return thread_; }
  /** @brief Return local address bound to this socket interface. */
  std::string address() const;
  /** @brief Return local network port bound to this socket interface. */
  uint16_t port() const;
  /** @brief Return remote peer destination IP address string. */
  std::string peerAddress() const;
  /** @brief Return local network port bound to this socket interface. */
  uint16_t peerPort() const;

protected:
  /** @brief Constructs an AbstractSocket with default parameters (AF_INET, SOCK_STREAM, IPPROTO_TCP) and binds it to the current thread context. @param mode The new mode for interpreting the buffer content. */
  AbstractSocket(OutputBufferMode = Application);
  /** @brief Constructs an AbstractSocket and binds it to the current thread context.
  @param family Address family. @param type Socket type. @param protocol Internet protocol. @param mode The new mode for interpreting the buffer content. */
  AbstractSocket(int, int, int, OutputBufferMode = Application);
  /** @brief Virtual destructor. Automatically detaches socket from thread if still attached. */
  virtual ~AbstractSocket();

  /** @brief Called when established connection.
  @details This method handles the internal non-blocking state machinery, managing background handshake operations before data transfer begins. */
  virtual void activateEvent();
  /** @brief Called when the connection state changes.
  @warning When state is State::Destroy, the socket thread pointer is guaranteed to be nullptr. */
  virtual void stateEvent() {}
  /** @brief Called when there is data to read. */
  virtual void readEvent() {}
  /** @brief Called when writing to the socket is possible. */
  virtual void writeEvent() {}
  /** @brief Called upon incoming connection. Executed on a server listening socket when a client connects. */
  virtual void incomingEvent() {}

  /** @brief Low-level system call that checks whether a file descriptor contains data.
  @return Number of raw bytes pending in the OS network buffer queue. */
  virtual int read_available_fd() const;
  /** @brief Low-level native operating system read operation proxying requests directly down to the fd handle. @param data Destination memory allocation chunk pointer. @param size Bounds metric tracking maximum allowed byte ingestion capability. @return Number of raw bytes read from the descriptor, or error status codes. */
  virtual int read_fd(void *, int);
  /** @brief Low-level native operating system write operation proxying requests directly up to the fd handle. @param data Source memory block holding raw serialization binary data. @param size Metric configuration identifying exactly how many bytes to transfer. @return Number of raw bytes written out to the descriptor, or error status codes. */
  virtual int write_fd(const void *, int);

  /** @brief Returns the number of bytes of data pending to be read. */
  int pendingRead() const;
  /** @brief Returns the number of bytes of data pending to be write. */
  int pendingWrite() const;

  /** @brief Configures or overrides the internal error code. @param code Target Error enum status representation to set. */
  void setError(Error);
  /** @brief Updates the human-readable error description buffer string. @param string Immutable string containing the diagnostic message. */
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
