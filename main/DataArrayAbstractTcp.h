/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArrayAbstractTcp.h @brief The DataArrayAbstractTcp class. */

#include "../core/FunctionConnector.h"
#include "ThreadPool.h"

namespace AsyncFw {
class DataArraySocket;
class DataArray;
class TlsContext;

/** @class DataArrayAbstractTcp DataArrayAbstractTcp.h <AsyncFw/DataArrayAbstractTcp> @brief Abstract base class implementing a multi-threaded TCP pool for socket and packet management.
@details Serves as the foundation for both DataArrayTcpServer and DataArrayTcpClient. It manages internal worker threads, applies uniform memory/buffer constraints, handles packet routing, and controls encryption policy rules. */
class DataArrayAbstractTcp : public AbstractThreadPool {
public:
  /** @enum Result @brief Defines error codes specific to asynchronous data transmission operations. */
  enum Result {
    ErrorTransmitInvoke = -100,    /**< Cross-thread invocation failure during data transmission. */
    ErrorTransmitNotActive = -101, /**< Transmission aborted because the target socket is not actively connected. */
    ErrorTransmit = -102           /**< Low-level or buffer-queue failure while sending data. */
  };

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
  /** @brief Asynchronously transmits a DataArray packet through a given socket context. @param socket Pointer to the target DataArraySocket. @param data Reference to the DataArray containing payload data. @param id Packet identification tag. @param wait If true, forces caller thread blocking until the buffer queues up. @return 0 on success, or a negative value from the Result enum on failure. */
  int transmit(const DataArraySocket *, const DataArray &, uint32_t, bool = false);
  /** @brief Signals a specific managed socket to disconnect from its remote peer. @param socket Pointer to the DataArraySocket instance to be disconnected. */
  void disconnectFromHost(const DataArraySocket *);
  /** @brief Gathers a list of all active managed sockets and counts them. @param list Optional destination vector to populate with socket pointers. @return The total count of active sockets running in the framework. */
  int sockets(std::vector<DataArraySocket *> * = nullptr);
  /** @brief Sets a strict list of remote IP addresses where TLS encryption must be bypassed. @param list Vector of IP addresses as strings. */
  void setEncryptionDisabled(const std::vector<std::string> &list) { disabledEncryptionHosts_ = list; }
  /** @brief Configures or overrides the TLS encryption toggle for a specific target address. @param address Remote host IP address string. @param disable If true, encryption is bypassed. */
  void setEncryptionDisabled(const std::string &, bool = true);
  /** @brief Assigns and initializes TLS security parameters on a specific socket. @param socket Pointer to the target DataArraySocket. @param context TLS certificates, keys, and security parameters. */
  void setTlsContext(DataArraySocket *, const TlsContext &);

  /** @brief Signal / Connector emitted when a complete DataArray packet is received by any pool socket.
  @note Emits the specific socket pointer, the received DataArray pointer, and its packet ID.
  @warning Buffer cleanup is fully automated internally after this signal processing completes. */
  FunctionConnector<const DataArraySocket *, const DataArray *, uint32_t>::Policy<AbstractFunctionConnector::DirectOnly>::Protected<DataArrayAbstractTcp> received;

protected:
  /** @class Thread @brief Worker thread context customized for handling specific groups of DataArraySockets. */
  class Thread : public AbstractThreadPool::Thread {
    friend class DataArrayAbstractTcp;

  public:
    /** @brief Constructs a new Thread instance within the pool framework. @param name Name string for thread tracking. @param _pool Pointer to the owner pool framework. */
    Thread(const std::string &name, DataArrayAbstractTcp *_pool) : AbstractThreadPool::Thread(name, _pool), pool(_pool) {};

  protected:
    /** @brief Configures timeouts, buffer sizes, and signal bounds for a new socket context. @param socket Pointer to the DataArraySocket being introduced to this thread. */
    void initSocket(DataArraySocket *);
    /** @brief Performs cleanup routines and safely removes a socket from the thread loop. @param socket Pointer to the target DataArraySocket. */
    void destroySocket(DataArraySocket *);
    AbstractThreadPool *pool; /**< Backward reference pointer to the base thread pool interface. */
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
};
}  // namespace AsyncFw
