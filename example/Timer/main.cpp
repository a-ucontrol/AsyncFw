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

AsyncFw::CoroutineTask task(AsyncFw::Timer *timer) {
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  co_await timer->coTimeout(10);
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  co_await timer->coTimeout(10);
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  AsyncFw::MainThread::exit(0);
}

int main(int argc, char *argv[]) {
  AsyncFw::Timer timer1;
  timer1.start(10);
  AsyncFw::Timer timer2;

  timer1.timeout.connect([]() { lsDebug() << std::chrono::system_clock::now() << " timer1 timeout"; });

  timer2.timeout.connect([]() { lsDebug() << std::chrono::system_clock::now() << " timer2 timeout"; });

  task(&timer2);

  lsNotice() << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton " << ret;
  return ret;
}
//! [snippet]
