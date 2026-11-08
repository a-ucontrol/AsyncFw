/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArrayAbstractTcp.h @brief The DataArrayAbstractTcp class. */

#include "../core/FunctionConnector.h"
#include "../core/TlsContext.h"
#include "ThreadPool.h"

namespace AsyncFw {
class DataArraySocket;
class DataArray;
class TlsContext;

/** @class DataArrayAbstractTcp DataArrayAbstractTcp.h <AsyncFw/DataArrayAbstractTcp> @brief Abstract base class implementing a multi-threaded TCP pool for socket and packet management.
@details Serves as the foundation for both DataArrayTcpServer and DataArrayTcpClient. It manages internal worker threads, applies uniform memory/buffer constraints, handles packet routing, and controls encryption policy rules. */
class DataArrayAbstractTcp : public AbstractThreadPool {
public:
  /** @brief Walks every live DataArraySocket across all workers of this pool and invokes f on the owning worker thread for each one.
  @details Wraps f in Invocable<void(const DataArraySocket*)>::Function and forwards to the protected sockets(Invocable*) overload, which takes ownership of the wrapper and deletes it before returning. The callable is passed by value, move-only functors are supported. The socket pointer is valid only for the duration of the call — the socket may be destroyed asynchronously afterwards.
  @tparam T Callable signature-compatible with void(const DataArraySocket*). @param f Callable invoked once per live socket on the worker thread. @return Total number of live sockets visited.
  @warning The visitor must not re-enter this pool (recursive sockets(), createSocket(), destroySocket(), threads()) and must not throw. */
  template <typename T, typename = std::enable_if_t<!std::is_convertible_v<T, Invocable<void(const DataArraySocket *)>::Abstract *>>>
  int sockets(T f) {
    return sockets(new Invocable<void(const DataArraySocket *)>::Function(std::forward<T>(f)));
  }
  /** @brief Count-only overload, returns the number of live DataArraySocket instances across all workers of this pool.
  @return Total number of live sockets. */
  int sockets() { return sockets(nullptr); }
  /** @brief Constructs a new DataArrayAbstractTcp instance. @param name Name identifier for the underlying thread pool. */
  DataArrayAbstractTcp(const std::string &);
  void init(int readTimeout = 30000, int waitKeepAliveAnswerTimeout = 0, int waitForEncryptionTimeout = 10000, int maxThreads = 4, int maxSockets = 8, int maxReadBuffers = 16, int maxReadSize = 16 * 1024 * 1024, int maxWriteBuffers = 16, int maxWriteSize = 16 * 1024 * 1024, int socketReadBufferSize = 1024 * 512) {
    this->readTimeout = readTimeout;
    this->waitKeepAliveAnswerTimeout = waitKeepAliveAnswerTimeout;
    this->waitForEncryptionTimeout = waitForEncryptionTimeout;
    this->maxThreads = maxThreads;
    this->maxSockets = maxSockets;
    this->maxReadBuffers = maxReadBuffers;
    this->maxReadSize = maxReadSize;
    this->maxWriteBuffers = maxWriteBuffers;
    this->maxWriteSize = maxWriteSize;
    this->socketReadBufferSize = socketReadBufferSize;
  }
  /** @brief Asynchronously transmits a DataArray packet through a given socket context. @param socket Pointer to the target DataArraySocket. @param data Reference to the DataArray containing payload data. @param id Packet identification tag. @return True on success. */
  bool transmit(const DataArraySocket *, const DataArray &, uint32_t);
  /** @brief Signals a specific managed socket to disconnect from its remote peer. @param socket Pointer to the DataArraySocket instance to be disconnected. */
  void disconnectFromHost(const DataArraySocket *);
  /** @brief Sets a strict list of remote IP addresses where TLS encryption must be bypassed. @param list Vector of IP addresses as strings. */
  void setEncryptionDisabled(const std::vector<std::string> &list) { disabledEncryptionHosts_ = list; }
  /** @brief Configures or overrides the TLS encryption toggle for a specific target address. @param address Remote host IP address string. @param disable If true, encryption is bypassed. */
  void setEncryptionDisabled(const std::string &, bool = true);
  /** @brief Assigns and initializes TLS security parameters on a specific socket. @param socket Pointer to the target DataArraySocket. @param context TLS certificates, keys, and security parameters. */
  void setTlsContext(DataArraySocket *, const TlsContext &);
  /** @brief Sets up the TLS credentials and configuration for secure client connections. @param context The TLS context object containing certificates and keys. */
  void setTlsContext(const TlsContext &context) { tlsContext = context; }

  /** @brief Signal / Connector emitted when a complete DataArray packet is received by any pool socket.
  @note Emits the specific socket pointer, the received DataArray pointer, and its packet ID.
  @warning Buffer cleanup is fully automated internally after this signal processing completes. */
  FunctionConnector<const DataArraySocket *, const DataArray *, uint32_t>::Policy<AbstractFunctionConnector::DirectOnly>::Protected<DataArrayAbstractTcp> received;

protected:
  /** @class Thread @brief Worker thread context customized for handling specific groups of DataArraySockets. */
  class Thread : public AbstractThreadPool::Thread {
    friend class DataArrayAbstractTcp;

  protected:
    using AbstractThreadPool::Thread::Thread;
    /** @brief Configures timeouts, buffer sizes, and signal bounds for a new socket context. @param socket Pointer to the DataArraySocket being introduced to this thread. */
    void initSocket(DataArraySocket *);
    /** @brief Performs cleanup routines and safely removes a socket from the thread loop. @param socket Pointer to the target DataArraySocket. */
    void destroySocket(DataArraySocket *);
  };
  /** @brief Locates the worker thread currently hosting the lowest number of socket contexts. @return Pointer to the least occupied Thread, or nullptr if no threads are running. */
  Thread *findMinimalSocketsThread();
  int readTimeout;                /**< Data absence timeout in milliseconds. */
  int waitKeepAliveAnswerTimeout; /**< Ping response timeout window in milliseconds. */
  int waitForEncryptionTimeout;   /**< TLS handshake maximum window in milliseconds. */
  std::size_t maxThreads;         /**< Allowed thread capacity configuration limit. */
  std::size_t maxSockets;         /**< Absolute global count threshold for sockets. */
  int socketReadBufferSize;       /**< System level network chunk buffer capability size. */
  int maxReadBuffers;             /**< Maximum incoming buffer blocks allocated per socket. */
  int maxReadSize;                /**< Absolute maximum byte bounds for inbound data allocation. */
  int maxWriteBuffers;            /**< Maximum outbound frame descriptors allocated per socket. */
  int maxWriteSize;               /**< Absolute maximum byte bounds for outbound transmission queue data. */
  /** @brief Internal array of remote endpoints exempted from TLS handshake logic. */
  std::vector<std::string> disabledEncryptionHosts_ = {"127.0.0.1"};
  TlsContext tlsContext;
  int sockets(Invocable<void(const DataArraySocket *)>::Abstract *);
};
}  // namespace AsyncFw
