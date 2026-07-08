/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArrayTcpClient.h @brief The DataArrayTcpClient class. */

#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
/** @class DataArrayTcpClient DataArrayTcpClient.h <AsyncFw/DataArrayTcpClient> @brief An asynchronous TCP client class managing outgoing server connections and synchronous exchanges. Provides capabilities to create managed sockets, handle background threads for data transmission, and execute blocking request-response cycles (exchanges) with timeouts. */
class DataArrayTcpClient : public DataArrayAbstractTcp {
protected:
  /** @class Thread @brief Internal worker thread class dedicated to managing client sockets. */
  class Thread : public DataArrayAbstractTcp::Thread {
    friend class DataArrayTcpClient;

  public:
    /** @brief Helper method to retrieve the parent client instance owning this thread pool. @return Pointer to the owning DataArrayTcpClient instance. */
    inline DataArrayTcpClient *client() { return static_cast<DataArrayTcpClient *>(pool); }

  private:
    using DataArrayAbstractTcp::Thread::Thread;
    ~Thread() override;
    DataArraySocket *createSocket();
  };
  /** @brief Internal handler triggered when an individual socket's state changes. @param socket Pointer to the socket whose state transitioned. */
  virtual void socketStateChanged(const DataArraySocket *);

public:
  /** @enum Result @brief Defines specific error codes returned during data exchange operations. */
  enum Result {
    ErrorExchangeNotActive = -103,       /**< Exchange aborted because the network loop or socket is inactive. */
    ErrorExchangeTransmit = -104,        /**< Failed to enqueue or transmit the request packet. */
    ErrorExchangeConnectionClose = -105, /**< Connection was dropped by the remote peer during exchange. */
    ErrorExchangeTimeout = -106,         /**< Timeout reached before the response frame was completely received. */
    ErrorExchangeThread = -107           /**< Thread synchronization or contextual framework error. */
  };
  /** @brief Constructs a new DataArrayTcpClient object. @param name Optional name identifier for the client thread/instance (defaults to "TcpClient"). */
  DataArrayTcpClient(const std::string & = "TcpClient");
  /** @brief Explicitly connects a specific socket to a target host and port. @param socket Pointer to the managed DataArraySocket instance. @param address Target IP address or hostname. @param port Target network port. @param timeout Connection timeout in milliseconds. If 0, uses the default timeout. */
  void connectToHost(DataArraySocket *socket, const std::string &, uint16_t, int = 0);
  /** @brief Initiates a connection using a pre-configured socket's internal host settings. @param socket Pointer to the managed DataArraySocket instance. @param timeout Connection timeout in milliseconds. If 0, uses the default timeout. */
  void connectToHost(const DataArraySocket *, int = 0);
  /** @brief Allocates and spawns a new worker thread within the client pool context. @return Pointer to the newly created Thread instance. */
  Thread *createThread();
  /** @brief Allocates a new socket instance and associates it with a specific worker thread. @param thread Pointer to the target Thread. If nullptr, a thread is selected automatically. @return Pointer to the freshly initialized DataArraySocket instance. */
  DataArraySocket *createSocket(Thread *thread = nullptr);
  /** @brief Safely shuts down, unregisters, and deletes a managed socket. @param socket Pointer to the DataArraySocket to destroy. */
  void destroySocket(DataArraySocket *);

  /** @brief Executes a synchronous request-response data exchange over a specific socket.
  @details Transmits the outgoing array, blocks the calling thread, and waits for the specific response frame.
  @param socket Pointer to the active socket executing the transmission. @param request The DataArray containing payload to be sent. @param response Pointer to a DataArray destination where incoming data will be loaded. @param id The transaction/packet identifier matching the response. @param timeout Maximum duration to wait for the completed response in milliseconds (defaults to 5000). @return 0 on success, or one of the negative codes from the Result enum on failure. */
  int exchange(const DataArraySocket *, const DataArray &, const DataArray *, uint32_t, int = 5000);
  /** @brief Returns the maximum number of concurrent sockets allowed in this client instance. @return Socket limit capacity. */
  std::size_t socketLimit() { return maxSockets; }
  /** @brief Sets the default timeout interval for establishing connections. @param timeout Timeout interval in milliseconds. */
  void setConnectTimeout(int timeout) { waitForConnectTimeoutInterval = timeout; }
  /** @brief Sets the interval delay before attempting to reconnect a disconnected socket. @param timeout Reconnection delay in milliseconds. */
  void setReconnectTimeout(int timeout) { reconnectTimeoutInterval = timeout; }

  /** @brief Signal / Connector triggered when any managed socket changes its connection status. */
  FunctionConnector<const DataArraySocket *>::Policy<AbstractFunctionConnector::DirectOnly>::Protected<DataArrayTcpClient> connectionStateChanged;

private:
  int waitForConnectTimeoutInterval = 10000;
  int reconnectTimeoutInterval = 10000;
};
}  // namespace AsyncFw
