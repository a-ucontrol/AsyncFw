#pragma once

#include <string>
#include "core/FunctionConnector.h"

namespace AsyncFw {
class AddressInfo {
  struct Private;

public:
  AddressInfo();
  ~AddressInfo();

  void resolve(const std::string &);
  void setTimeout(int _timeout) { timeout_ = _timeout; }

  FunctionConnectorProtected<AddressInfo>::Connector<int, const std::vector<std::string> &> completed;

private:
  struct Private *private_;
  int timeout_ = 10000;
};
}  // namespace LRW
