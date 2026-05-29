/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file SystemProcess.h @brief The SystemProcess class. */

#include <string>
#include "../core/FunctionConnector.h"

namespace AsyncFw {
/** @class SystemProcess SystemProcess.h <AsyncFw/SystemProcess> @brief The SystemProcess class. \warning Unix-like systems only
@brief Example: @snippet SystemProcess/main.cpp snippet */
class SystemProcess {
public:
  enum State : uint8_t { None, Running, Finished, Crashed, Error };
  /** @param redirect_stdin
    @brief False Управляет вводом данных в запущенный процесс. Это режим ввода по умолчанию для канала ввода
    @brief True  Дочерний процесс перенаправляет ввод основного процесса на работающий процесс. Дочерний процесс считывает свой стандартный ввод из того же источника, что и основной процесс. Обратите внимание, что основной процесс не должен пытаться считывать свой стандартный ввод, пока работает дочерний процесс. */
  SystemProcess(bool = false);
  ~SystemProcess();
  bool start(const std::string &, const std::vector<std::string> & = {});
  bool start();
  State state();
  pid_t pid();
  void wait();
  int exitCode();
  bool input(const std::string &) const;

  /** @brief The FunctionConnector for SystemProcess stateChanged. */
  FunctionConnector<State>::Protected<SystemProcess> stateChanged {AbstractFunctionConnector::Queued};
  /** @brief The FunctionConnector for SystemProcess output. */
  FunctionConnector<const std::string &, bool /*stdout: 0, stderr: 1*/>::Protected<SystemProcess> output {AbstractFunctionConnector::Queued};

  template <typename T>
  static bool exec(const std::string &cmd, const std::vector<std::string> &args, T f) {
    return exec_(cmd, args, new Invocable<void(int, State, const std::string &, const std::string &)>::Function(std::forward<T>(f)));
  }
  template <typename T>
  static bool exec(const std::string &cmd, T f) {
    return exec_(cmd, {}, new Invocable<void(int, State, const std::string &, const std::string &)>::Function(std::forward<T>(f)));
  }
  static bool exec(const std::string &cmd, const std::vector<std::string> &args = {}) { return exec_(cmd, args, nullptr); }

private:
  static bool exec_(const std::string &, const std::vector<std::string> &, Invocable<void(int, State, const std::string &, const std::string &)>::Abstract *);
  void finality();
  struct Private;
  Private &private_;
};

}  // namespace AsyncFw
