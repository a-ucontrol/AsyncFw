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
/** @class SystemProcess @brief Manages spawning and asynchronous communication with external processes.
@warning Optimized for Unix-like systems. Maintains cross-platform behavior when compiled with Qt.
@note Life cycle events are driven by AbstractFunctionConnector::Queued policy.
@brief Example: @snippet SystemProcess/main.cpp snippet */
class SystemProcess {
public:
  /** @enum State @brief Process lifecycle states.*/
  enum State : uint8_t {
    None,      ///< Process is not initialized or has been reset.
    Running,   ///< Process is currently running.
    Finished,  ///< Process finished normally with a valid exit code.
    Crashed,   ///< Process terminated abnormally by OS or crash.
    Error      ///< Failed to fork, exec, or open I/O channels.
  };
  /** @brief Constructs a new SystemProcess manager.
  @param redirect_stdin **False (Default):** Input is written manually via input() method. **True:** Child process shares the standard input (stdin) with the parent process.
  @warning When True, parent process must not read from stdin while child is active. */
  SystemProcess(bool = false);
  /** @brief Destructor. Synchronously releases internal data structures. */
  ~SystemProcess();
  /** @brief Starts the process with the specified command and arguments. @param cmd Executable path or system command. @param args Arguments vector passed to the process. @return True if successfully initiated. */
  bool start(const std::string &, const std::vector<std::string> & = {});
  /** @brief Starts the process using stored command and arguments. @return True if successfully initiated. */
  bool start();
  /** @brief Gets current process lifecycle state. @return Current state from SystemProcess::State enum. */
  State state();
  /** @brief Gets system process identifier (PID). @return Operating system pid_t handle. */
  pid_t pid();
  /** @brief Blocks current execution frame until process finishes. Spins a nested Event Loop, allowing other thread tasks to execute in parallel. */
  void wait();
  /** @brief Gets operating system exit status code. @return Integer exit code. Valid only when state() is Finished or Crashed. */
  int exitCode();
  /** @brief Writes data string to standard input (stdin) of the child process. @param str Raw string buffer. @return true if successfully written; false otherwise. */
  bool input(const std::string &) const;

  /** @brief Emitted asynchronously when the process execution state changes. */
  FunctionConnector<State>::Policy<AbstractFunctionConnector::Queued>::Protected<SystemProcess> stateChanged;
  /** @brief Emitted when data arrives from stdout (false) or stderr (true). */
  FunctionConnector<const std::string &, bool /*stdout: 0, stderr: 1*/>::Policy<AbstractFunctionConnector::Queued>::Protected<SystemProcess> output;

  /** @brief Static helper to spawn a managed process inside heap (fire-and-forget) with custom callback. @param f Callback signature: void(int exitCode, State state, const std::string& out, const std::string& err) */
  template <typename T>
  static bool exec(const std::string &cmd, const std::vector<std::string> &args, T f) {
    return exec_(cmd, args, new Invocable<void(int, State, const std::string &, const std::string &)>::Function(std::forward<T>(f)));
  }
  /** @brief Static helper to spawn a managed process without arguments. */
  template <typename T>
  static bool exec(const std::string &cmd, T f) {
    return exec_(cmd, {}, new Invocable<void(int, State, const std::string &, const std::string &)>::Function(std::forward<T>(f)));
  }
  /** @brief Static helper to spawn a background process with no tracking hooks. */
  static bool exec(const std::string &cmd, const std::vector<std::string> &args = {}) { return exec_(cmd, args, nullptr); }

private:
  static bool exec_(const std::string &, const std::vector<std::string> &, Invocable<void(int, State, const std::string &, const std::string &)>::Abstract *);
  void finality();
  struct Private;
  Private &private_;
};

}  // namespace AsyncFw
