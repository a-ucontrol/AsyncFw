/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

AsyncFw::CoroutineTask task(AsyncFw::Timer *timer) {
  lsDebug() << "coro task - 1";
  co_await timer->coTimeout(10);
  lsDebug() << "coro task - 2";
  co_await timer->coTimeout(10);
  lsDebug() << "coro task - 3";
  AsyncFw::MainThread::exit(0);
}

int main(int, char **) {
  AsyncFw::LogStream::setTimeFormat("%Y-%m-%d %H:%M:%S", true);
  AsyncFw::Timer timer1;
  timer1.start(10);
  AsyncFw::Timer timer2;

  timer1.timeout.connect([]() { lsDebug() << " timer1 timeout"; });

  timer2.timeout.connect([]() { lsDebug() << " timer2 timeout"; });

  task(&timer2);

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application " << ret;
  return ret;
}
//! [snippet]
