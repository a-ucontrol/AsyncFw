/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file RrdClient.h @brief The RrdClient class. */

#include "../core/FunctionConnector.h"

namespace AsyncFw {
class Rrd;
class DataArray;
class TlsContext;
class DataArraySocket;

/** @class RrdClient RrdClient.h <AsyncFw/RrdClient> @brief A specialized network client class for synchronizing remote Round-Robin Databases (Rrd).
@details Manages an underlying DataArraySocket to establish communication with an RrdServer. It coordinates a group of local Rrd circular databases, tracks their latest record indexes, and handles secure data ingestion over TCP with optional TLS support. */
class RrdClient {
public:
  /** @brief Constructs an RrdClient instance. @param socket Pointer to the managed socket used for the network connection. @param rrd Vector of local Rrd circular databases to be populated/synchronized. */
  RrdClient(DataArraySocket *, const std::vector<Rrd *> &);
  /** @brief Destructor. Cleans up internal signal connection guards and pending synchronization timers. */
  ~RrdClient();
  /** @brief Initiates a connection to an RrdServer at the specified remote address and port. @param address IP address or domain name of the remote host server. @param port Network port of the remote host server. */
  void connectToHost(const std::string &, uint16_t);
  /** @brief Connects to the host using pre-configured remote endpoints stored in the underlying socket. */
  void connectToHost();
  /** @brief Explicitly disconnects the underlying client socket from the remote RrdServer. */
  void disconnectFromHost();
  /** @brief Transmits a raw data frame or a custom query command to the remote server. @param data The DataArray payload containing the request or command. @param id Network packet transaction identifier. @param wait If true, blocks the calling thread until the data is successfully queued or sent. @return 0 or a positive tracking code on success, or a negative error status on failure.
  @note The packetId (pi) corresponds to the target Rrd database index and must not exceed 0x0F.*/
  bool transmit(const DataArray &, uint32_t, bool = false);
  /** @brief Sets up TLS security context and credentials for private communications. @param context TLS certificates, keys, and security parameters. */
  void tlsSetup(const TlsContext &);

private:
  std::vector<Rrd *> rrd_;
  DataArraySocket *tcpSocket;
  int requestTimerId;
  std::vector<uint64_t> lastTime;
  void tcpRead(const DataArray *, uint32_t);
  void request(int);
  FunctionConnectionGuardList gl_;
};
}  // namespace AsyncFw
