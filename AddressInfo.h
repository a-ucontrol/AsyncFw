/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>
#include "core/FunctionConnector.h"
#include "Coroutine.h"

#ifndef _WIN32
  #define AF_UNSPEC_ 0
  #define AF_INET_ 2
  #define AF_INET6_ 10
#else
  #define AF_UNSPEC_ 0
  #define AF_INET_ 2
  #define AF_INET6_ 23
#endif

namespace AsyncFw {
/*! \brief The AddressInfo class. */
class AddressInfo {
  struct Private;

public:
  using Result = std::vector<std::string>;
  enum Family : uint8_t { Unspec = AF_UNSPEC_, Inet = AF_INET_, Inet6 = AF_INET6_ };
  AddressInfo();
  ~AddressInfo();

  void resolve(const std::string &, Family = Inet);
  void setTimeout(int);

  FunctionConnectorProtected<AddressInfo>::Connector<int, const std::vector<std::string> &> completed;
  AsyncFw::CoroutineAwait coResolve(const std::string &, Family = Inet);

private:
  struct Private *private_;
};
}  // namespace AsyncFw
