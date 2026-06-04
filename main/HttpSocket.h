/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file HttpSocket.h @brief The HttpSocket class. */

#include "../core/FunctionConnector.h"
#include "../core/DataArray.h"
#include "../core/AbstractTlsSocket.h"
#include "File.h"

namespace AsyncFw {
/** @class HttpSocket HttpSocket.h <AsyncFw/HttpSocket> @brief The HttpSocket class.
@brief Example: @snippet HttpSocket/main.cpp snippet */
class HttpSocket : public AsyncFw::AbstractTlsSocket {
  friend LogStream &operator<<(LogStream &, const HttpSocket &);

public:
  static HttpSocket *create(AsyncFw::Thread *_t = nullptr);

  void stateEvent() override;
  void activateEvent() override;
  void readEvent() override;
  void writeEvent() override;

  DataArrayView header();
  DataArrayView content();
  void sendFile(const std::string &);

  FunctionConnector<const AbstractSocket::State>::Protected<HttpSocket> stateChanged;
  FunctionConnector<const DataArray &>::Protected<HttpSocket> received;
  FunctionConnector<>::Protected<HttpSocket> writeContent;
  FunctionConnector<int>::Protected<HttpSocket> progress;

protected:
  HttpSocket();
  ~HttpSocket();
  void clearReceived();
  bool connectionClose = false;

private:
  DataArray received_;
  File file_;
  int progress_;
  int headerSize_;
  std::size_t contentLenght_ = std::string::npos;
  int tid_ = -1;
  bool full_ = false;
};
}  // namespace AsyncFw
