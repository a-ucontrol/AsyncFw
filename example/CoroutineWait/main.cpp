/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Coroutine>
#include <AsyncFw/Timer>
#include <AsyncFw/Log>

#define CURRENT_TIME_MS (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

using namespace AsyncFw;

CoroutineTask task(uint64_t &end, int _ms) {
  logDebug() << "task started";
  co_await CoroutineAwait([&end, &_ms](CoroutineHandle _h) {
    ThreadPool::async(
        [&_ms]() {
          logDebug() << "async" << _ms << Thread::currentThread()->name();
          std::this_thread::sleep_for(std::chrono::milliseconds(_ms));
          logDebug() << "async end" << _ms << Thread::currentThread()->name();
        },
        [&end, _h]() {
          logDebug() << "resume";
          end = CURRENT_TIME_MS;
          _h.resume();
          logDebug() << "resume end";
        });
  });
  logDebug() << "task finished";
}

void tst(int _ms) {
  uint64_t tst_time = CURRENT_TIME_MS;
  logNotice() << _ms << "start";
  uint64_t ms_start = CURRENT_TIME_MS;
  logDebug() << _ms << "START" << ms_start;
  uint64_t ms_end;
  CoroutineTask _ct = task(ms_end, _ms);
  _ct.wait();
  logNotice() << _ms << ms_end - ms_start << "=====" << CURRENT_TIME_MS - tst_time;
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineWaitExamplePool", 5);

  //tst(5000);

  Timer::single(10, []() { tst(10); });
  Timer::single(10, []() { tst(15); });
  Timer::single(10, []() { tst(20); });
  Timer::single(10, []() { tst(50); });
  Timer::single(10, []() { tst(10); });

  Timer::single(100, []() {
    logNotice() << "MainThread::quit()";
    MainThread::exit();
  });

  logNotice() << "MainThread::exec()";

  return MainThread::exec();
}
