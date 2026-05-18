/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  int cnt = 0;
  AsyncFw::Timer timer1;
  timer1.start(10);
  AsyncFw::Timer timer2;
  timer2.start(20);

  timer1.timeout.connect([&cnt]() {
    lsDebug() << std::chrono::system_clock::now() << " timer1 timeout";
    if (++cnt == 10) AsyncFw::MainThread::exit(0);
  });

  timer2.timeout.connect([]() { lsDebug() << std::chrono::system_clock::now() << " timer2 timeout"; });

  lsNotice() << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton " << ret;
  return ret;
}
//! [snippet]
