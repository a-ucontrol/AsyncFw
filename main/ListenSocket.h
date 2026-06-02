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
/** @class ListenSocket ListenSocket.h <AsyncFw/ListenSocket> @brief The ListenSocket class.
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
  AsyncFw::FunctionConnector<int, const std::string &, bool *>::Policy<AsyncFw::AbstractFunctionConnector::SyncOnly>::Protected<ListenSocket> incoming;

protected:
  void incomingEvent() override;
};

}  // namespace AsyncFw
