/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/Version>
#include <AsyncFw/MainThread>
#include <AsyncFw/Log>

int main(int argc, char *argv[]) {
  // 1. Create a dedicated background thread for safe, non-blocking file logging
  AsyncFw::Thread logThread {"LogThread"};
  logThread.start();

  // 2. Instantiate the logger engine inside the background thread to isolate file I/O operations
  logThread.invoke([]() { AsyncFw::Instance<AsyncFw::Log>::create(1000, "log-file-name"); }, true);

  // 3. Force colorized terminal outputs for better log readability
  AsyncFw::Log::instance()->setColorOut(true);

  // 4. Log application metadata
  logNotice() << "Version:" << AsyncFw::Version::str();
  logNotice() << "Git:" << AsyncFw::Version::git();

  // 5. Inline streaming style macros (ls*): Print FULL function signatures (__PRETTY_FUNCTION__)
  // These macros can be completely stripped out at compile-time (e.g. via #define LS_NO_TRACE)
  lsTrace() << "Trace";
  lsDebug() << "Debug";
  lsInfo() << "Info";
  lsInfoRed() << "Info red";
  lsInfoGreen() << "Info green";
  lsInfoBlue() << "Info blue";
  lsInfoMagenta() << "Info magenta";
  lsInfoCyan() << "Info cyan";
  lsNotice() << "Notice";
  lsWarning() << "Warning";
  lsError() << "Error";

  // 6. Modern formatting style macros (log*): Print only the parsed CLASS NAME or "Application"
  // Unlike ls* macros, these are always compiled into the binary
  logTrace() << "Trace";
  logDebug() << "Debug";
  logInfo() << "Info";
  logNotice() << "Notice";
  logWarning() << "Warning";
  logError() << "Error";
  logAlert() << "logAlert";
  // Un-commenting logEmergency will immediately call std::terminate() because of the Emergency log level
  //logEmergency() << "Emergency";

  // 7. Request graceful application loop shutdown via the current thread context
  AsyncFw::Thread::current()->invoke([]() { AsyncFw::MainThread::exit(); });

  // 8. Start the master application execution framework loop
  int ret = AsyncFw::MainThread::exec();

  // 9. Directly push a raw, structured record into the file logging pipe after loop termination
  AsyncFw::Log::instance()->append({AsyncFw::LogStream::Info, "Log", "Application finished", ""});

  return ret;
}
//! [snippet]
