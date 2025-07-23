#pragma once

#include <string>
#include "core/FunctionConnector.h"

namespace AsyncFw {
class SystemProcess {
public:
  enum State : uint8_t { None, Running, Finished, Crashed, Error };
  SystemProcess();
  ~SystemProcess();
  bool start(const std::string &, const std::vector<std::string> & = {});
  bool start();
  State state();
  pid_t pid();
  void wait();
  int exitCode();

  FunctionConnectorProtected<SystemProcess>::Connector<State> stateChanged;
  FunctionConnectorProtected<SystemProcess>::Connector<const std::string &, bool /*stdout: 0, stderr: 1*/> output;

  static FunctionConnectorProtected<SystemProcess>::Connector<int, State, const std::string &, const std::string &> &exec(const std::string &, const std::vector<std::string> &);

private:
  struct Private;
  Private *private_;
  void finality();
};

}  // namespace AsyncFw
