/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/SystemProcess>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
#ifdef USE_QAPPLICATION
  QCoreApplication app(argc, argv);
#endif

  AsyncFw::SystemProcess process;
  process.output([](const std::string &str, bool err) {
    if (!err) logInfo() << "OUT:" << '\n' + str;
    else { logError() << "ERR:" << '\n' + str; }
  });
  process.stateChanged([](AsyncFw::SystemProcess::State _s) {
    if (_s == AsyncFw::SystemProcess::Running) return;
    AsyncFw::MainThread::exit(0);
  });
#ifndef _WIN32
  bool _r = process.start("/bin/bash");
  if (!_r) {
    logNotice() << "Start '/bin/bash' error";
    return 0;
  }
  AsyncFw::Timer::single(10, [&process]() { process.input("echo 1234567890\n"); });
  AsyncFw::Timer::single(20, [&process]() { process.input("_cmd_\n"); });  //error: _cmd_ not found
  AsyncFw::Timer::single(30, [&process]() { process.input("ls\n"); });
  AsyncFw::Timer::single(40, [&process]() { process.input("exit\n"); });
#else
  bool _r = process.start("cmd.exe");
  if (!_r) {
    logNotice() << "Start 'cmd.exe' error";
    return 0;
  }
  AsyncFw::Timer::single(10, [&process]() { process.input("echo 1234567890\n"); });
  AsyncFw::Timer::single(20, [&process]() { process.input("_cmd_\n"); });  //error: _cmd_ not found
  AsyncFw::Timer::single(30, [&process]() { process.input("dir\n"); });
  AsyncFw::Timer::single(40, [&process]() { process.input("exit\n"); });
#endif
  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
//! [snippet]
