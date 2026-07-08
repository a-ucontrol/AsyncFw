/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file RrdServer.h @brief The RrdServer class. */

#include "../core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Rrd;

/** @class RrdServer RrdServer.h <AsyncFw/RrdServer> @brief A specialized network server component that exposes Round-Robin Databases (Rrd) via TCP.
@details Binds an instance of DataArrayTcpServer to a collection of Rrd database tracks (metrics, logs, etc.). It automatically listens for incoming client data synchronization queries, extracts historical slices from the ring buffers, and transmits them back to the connected sockets. */
class RrdServer {
public:
  /** @brief Constructs an RrdServer instance. @param server Pointer to the pre-configured TCP server that will handle socket connections. @param rrd Vector of pointers to the managed Rrd circular databases exposed for synchronization. */
  RrdServer(DataArrayTcpServer *, const std::vector<Rrd *> &);
  /** @brief Virtual destructor. Cleans up internal operational connection guards and network bindings. */
  virtual ~RrdServer();
  /** @brief Explicitly stops the server component and disconnects from the underlying TCP server events. */
  void quit();

protected:
  /** @brief Extracts a specific historical data slice from an Rrd archive and transmits it to a client socket.
  @details  This method acts as the core response handler. When a client requests telemetry or logs, this method
  reads records starting from a specific timeline index/timestamp up to the requested size, pack into a
  DataArray frame, and sends it asynchronously.
  @param socket Pointer to the destination client socket context. @param index The starting timeline primary index or timestamp boundary to slice from. @param size Maximum number of data items or chunk size bounds to extract from the target Rrd. @param pi The network packet transaction identifier to match the response with the request. */
  void transmit(const DataArraySocket *, uint64_t, uint32_t, uint32_t);
  DataArrayTcpServer *tcpServer; /**< Pointer to the underlying network TCP server listener pipeline. */
private:
  std::vector<Rrd *> rrd_; /* The internal collection of exposed metrics or logging circular databases. */
  FunctionConnectionGuard g_;
};
}  // namespace AsyncFw
