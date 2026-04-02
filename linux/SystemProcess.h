/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>
#include "../core/FunctionConnector.h"

namespace AsyncFw {
/*! \brief The SystemProcess class. \warning Unix-like systems only
 \brief Example: \snippet SystemProcess/main.cpp snippet */
class SystemProcess {
public:
  enum State : uint8_t { None, Running, Finished, Crashed, Error };
  SystemProcess(bool = false);
  ~SystemProcess();
  bool start(const std::string &, const std::vector<std::string> & = {});
  bool start();
  State state();
  pid_t pid();
  void wait();
  int exitCode();
  bool input(const std::string &) const;

  /*! \brief The FunctionConnector for SystemProcess stateChanged. */
  FunctionConnectorProtected<SystemProcess>::Connector<State> stateChanged {AbstractFunctionConnector::Queued};
  /*! \brief The FunctionConnector for SystemProcess output. */
  FunctionConnectorProtected<SystemProcess>::Connector<const std::string &, bool /*stdout: 0, stderr: 1*/> output {AbstractFunctionConnector::Queued};

  template <typename T>
  static bool exec(const std::string &cmd, const std::vector<std::string> &args, T f) {
    return exec_(cmd, args, new FunctionArgs<int, State, const std::string &, const std::string &>::Function<T>(f));
  }
  template <typename T>
  static bool exec(const std::string &cmd, T f) {
    return exec_(cmd, {}, new FunctionArgs<int, State, const std::string &, const std::string &>::Function<T>(f));
  }
  static bool exec(const std::string &cmd, const std::vector<std::string> &args = {}) { return exec_(cmd, args, nullptr); }

private:
  static bool exec_(const std::string &, const std::vector<std::string> &, AbstractFunction<int, State, const std::string &, const std::string &> *);

  void finality();
  struct Private;
  Private *private_;
};

}  // namespace AsyncFw
