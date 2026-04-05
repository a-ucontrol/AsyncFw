/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "../core/AbstractSocket.h"
#include "../core/FunctionConnector.h"

namespace AsyncFw {
/*! \class ListenSocket ListenSocket.h <AsyncFw/ListenSocket> \brief The ListenSocket class.
 \brief Example: \snippet ListenSocket/main.cpp snippet */
class ListenSocket : private AbstractSocket {
public:
  using AbstractSocket::address;
  using AbstractSocket::close;
  using AbstractSocket::destroy;
  using AbstractSocket::listen;
  using AbstractSocket::port;
  ~ListenSocket();
  /*! \brief The FunctionConnector for incoming connections. */
  AsyncFw::FunctionConnectorProtected<ListenSocket>::Connector<int, const std::string &, bool *> incoming {AsyncFw::AbstractFunctionConnector::SyncOnly};

protected:
  void incomingEvent() override;
};

}  // namespace AsyncFw
