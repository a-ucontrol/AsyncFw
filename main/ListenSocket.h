/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file ListenSocket.h @brief The ListenSocket class. */

#include "../core/AbstractSocket.h"
#include "../core/FunctionConnector.h"

namespace AsyncFw {
/** @class ListenSocket ListenSocket.h <AsyncFw/ListenSocket> @brief Server-side socket wrapper specialized for listening and accepting incoming network connections.
@details Leverages private inheritance from AbstractSocket to safely expose only server-specific management methods while encapsulating and hiding client-centric I/O routines.
@brief Example: @snippet ListenSocket/main.cpp snippet */
class ListenSocket : private AbstractSocket {
public:
  using AbstractSocket::address;
  using AbstractSocket::close;
  using AbstractSocket::destroy;
  using AbstractSocket::listen;
  using AbstractSocket::port;
  ~ListenSocket();
  /** @brief The FunctionConnector for incoming connections. */
  /** @brief Synchronous signal connector emitted immediately upon a new incoming connection.
  @details Slots subscribing to this connector must accept: @n - int: The newly created raw socket file descriptor for the incoming client. @n - const std::string &: The remote client's IPv4/IPv6 address string. @n - bool *: An out-parameter flag pointer to signal back if the connection should be accepted or rejected. */
  AsyncFw::FunctionConnector<int, const std::string &, bool *>::Policy<AsyncFw::AbstractFunctionConnector::SyncOnly>::Protected<ListenSocket> incoming;

protected:
  void incomingEvent() override;
};

}  // namespace AsyncFw
