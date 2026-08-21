/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArraySocket.h @brief The DataArraySocket class. */

#include "../core/AbstractTlsSocket.h"
#include "../core/FunctionConnector.h"

namespace AsyncFw {
class TlsContext;
/** @class DataArraySocket DataArraySocket.h <AsyncFw/DataArraySocket> @brief An asynchronous socket class for transmitting data arrays (DataArray) with TLS support. Manages high-level data packet processing, read/write buffer boundaries, timeout intervals, keep-alive monitoring, and integration with TLS layers. */
class DataArraySocket : public AbstractTlsSocket {
  friend class DataArrayAbstractTcp;
  friend class DataArrayTcpServer;
  friend class DataArrayTcpClient;
  friend LogStream &operator<<(LogStream &, const DataArraySocket &);

public:
  /** @brief Asynchronously transmits a data array through the socket. @param da Reference to the DataArray being sent. @param id The packet identifier. @param wait If true and called from outside the socket thread, blocks the thread until completion. @return True if the data was successfully queued for transmission. */
  bool transmit(const DataArray &, uint32_t, bool = false) const;
  /** @brief Sets the timeout interval for connection establishment. @param timeout Timeout interval in milliseconds. */
  void setConnectTimeout(int timeout);
  /** @brief Sets the timeout interval for automatic reconnection upon disconnection. @param timeout Timeout interval in milliseconds. */
  void setReconnectTimeout(int timeout);
  /** @brief Sets the data read operation timeout interval. @param timeout Maximum allowed data absence in milliseconds before dropping the connection. */
  void setReadTimeout(int timeout);
  /** @brief Sets the timeout interval for establishing a TLS handshake. @param timeout Timeout interval in milliseconds. */
  void setWaitForEncryptionTimeout(int timeout);
  /** @brief Sets the timeout interval for receiving a keep-alive response. @param timeout Timeout in milliseconds. Setting to 0 disables keep-alive monitoring. */
  void setWaitKeepAliveResponseTimeout(int timeout);
  /** @brief Configures limits for incoming (read) data buffers. @param buffers Maximum number of simultaneously stored read buffers. @param size Maximum size of a single read buffer in bytes. */
  void setReadBuffers(int buffers, int size);
  /** @brief Configures limits for outgoing (write) data buffers. @param buffers Maximum number of packet chunks allowed in the transmit queue. @param size Maximum cumulative size of the transmission data in bytes. */
  void setWriteBuffers(int buffers, int size);
  /** @brief Initializes the socket for a server-side connection (handling an accepted client). */
  void initServerConnection();
  /** @brief Configures the remote host address and port for subsequent connection attempts. @param address IP address or domain name of the remote host. @param port Network port of the remote host. */
  void setHost(const std::string &address, uint16_t port) const;
  /** @brief Gets the remote host address previously assigned via setHost. @return The host address string. */
  const std::string hostAddress() const;
  /** @brief Gets the remote host port previously assigned via setHost. @return The host network port. */
  uint16_t hostPort() const;
  /** @brief Forces the transmission of a keep-alive request (ping) to the remote peer. */
  void sendKeepAlive() { sendKeepAlive(true); }
  /** @brief Gets the current internal state of the socket. @return An AbstractSocket::State enum value. */
  AbstractSocket::State state() const { return state_; }
  /** @brief Initiates a connection to the specified remote host and port. @param address Destination IP address. @param port Destination network port. @return True if the connection process was successfully started. */
  bool connect(const std::string &, uint16_t) override;
  /** @brief Disconnects the socket from the remote peer. */
  void disconnect() override;
  /** @brief Frees the memory allocated for the read buffer associated with the given pointer. @param da Pointer to the data array that is no longer needed. */
  void releaseBuffer(const DataArray *) const;

  /** @brief Signal / Connector triggered when the socket state changes. */
  FunctionConnector<AbstractSocket::State>::Policy<AbstractFunctionConnector::DirectOnly>::Protected<DataArraySocket> stateChanged;
  /** @brief Signal / Connector triggered when a complete data array is successfully received and parsed.
  @note Passes a pointer to the DataArray and its ID. The receiver is responsible for freeing the buffer by calling releaseBuffer(). */
  FunctionConnector<const DataArray *, uint32_t>::Policy<AbstractFunctionConnector::DirectOnly>::Protected<DataArraySocket> received;

protected:
  DataArraySocket();
  ~DataArraySocket() override;
  void stateEvent() override;
  void readEvent() override;
  using AbstractTlsSocket::connect;

private:
  bool connectToHost();
  bool connectToHost(int);
  void sendKeepAlive(bool);
  void writeSocket();
  void startTimer(int);
  void removeTimer();
  void timerEvent();
  std::string peerString() const;
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
