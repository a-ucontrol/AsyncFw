#pragma once

#include <string>
#include "core/FunctionConnector.h"

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
class AddressInfo {
  struct Private;

public:
  enum Family : uint8_t { Unspec = AF_UNSPEC_, Inet = AF_INET_, Inet6 = AF_INET6_ };
  AddressInfo();
  ~AddressInfo();

  void resolve(const std::string &, Family = Inet);
  void setTimeout(int);

  FunctionConnectorProtected<AddressInfo>::Connector<int, const std::vector<std::string> &> completed;

private:
  struct Private *private_;
};
}  // namespace AsyncFw
