/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArrayTcpServer.h @brief The DataArrayTcpServer class. */

#include "../core/TlsContext.h"
#include "ListenSocket.h"
#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
class ListenSocket;
/** @class DataArrayTcpServer DataArrayTcpServer.h <AsyncFw/DataArrayTcpServer> @brief An asynchronous TCP server class managing client connections and TLS security. Inherits from DataArrayAbstractTcp to utilize a thread pool model. It listens for incoming connections, assigns them to worker threads, and configures TLS context for secure communications. */
class DataArrayTcpServer : public DataArrayAbstractTcp {
public:
  using DataArrayAbstractTcp::setTlsContext;
  /** @brief Constructs a new DataArrayTcpServer object. @param name Optional name identifier for the server thread/instance (defaults to "TcpServer"). */
  DataArrayTcpServer(const std::string & = "TcpServer");
  /** @brief Stops the server and terminates all active worker threads and connections. */
  void quit() override;
  /** @brief Starts listening for incoming connections on the specified address and port. @param address IP address to bind to (e.g., "0.0.0.0" or "127.0.0.1"). @param port Network port to listen on. @return true if the server successfully started listening, otherwise false. */
  bool listen(const std::string &address, uint16_t port);
  /** @brief Closes the listening socket, preventing new connections while keeping current ones active. */
  void close();
  /** @brief Sets a list of remote addresses that are prioritized or persistently connected. @param list Vector of IP addresses. */
  void setAlwaysConnect(const std::vector<std::string> &list);
  /** @brief Checks whether the server is currently actively listening for incoming connections. @return true if listening, otherwise false. */
  bool listening();
  /** @brief Sets up the TLS credentials and configuration for secure client connections. @param context The TLS context object containing certificates and keys. */
  void setTlsContext(const TlsContext &context) { tlsData = context; }

private:
  class Thread : public DataArrayAbstractTcp::Thread {
    friend class DataArrayTcpServer;

  public:
    inline DataArrayTcpServer *server() { return static_cast<DataArrayTcpServer *>(pool); }

  private:
    using DataArrayAbstractTcp::Thread::Thread;
    void createSocket(int, bool);
  };
  std::unique_ptr<ListenSocket> listener;

  bool incomingConnection(int, const std::string &);
  std::vector<std::string> alwaysConnect_;
  TlsContext tlsData;
};
}  // namespace AsyncFw
