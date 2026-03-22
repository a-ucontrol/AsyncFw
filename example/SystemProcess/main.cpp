/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <MainThread.h>
#include <linux/SystemProcess.h>
#include <Timer.h>
#include <core/LogStream.h>

int main(int argc, char *argv[]) {
  AsyncFw::SystemProcess process;
  process.output([](const std::string &str, bool err) {
    if (!err) logInfo() << "OUT:" << '\n' + str;
    else { logError() << "ERR:" << '\n' + str; }
  });
  process.stateChanged([](AsyncFw::SystemProcess::State _s) {
    if (_s == AsyncFw::SystemProcess::Running) return;
    AsyncFw::MainThread::exit(0);
  });

  process.start("/bin/bash");

  AsyncFw::Timer::single(50, [&process]() { process.input("echo 1234567890\n"); });
  AsyncFw::Timer::single(100, [&process]() { process.input("_cmd_\n"); });  //error: _cmd_ not found
  AsyncFw::Timer::single(150, [&process]() { process.input("ls /\n"); });
  AsyncFw::Timer::single(200, [&process]() { process.input("exit\n"); });

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
//! [snippet]
