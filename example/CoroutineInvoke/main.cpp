/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/Coroutine>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

class Example {
public:
  double tst_double(int v1, double v2) {
    lsDebug() << v1 << v2 << Thread::current()->name();
    std::chrono::milliseconds(10);
    return v1 + v2 + 10000.0;
  }
  void tst_void(const std::string &s) { lsDebug() << s << Thread::current()->name(); }
};

CoroutineTask task() {
  Thread _thread = Thread("CoroutineInvokeAwait");
  _thread.start();

  double i = co_await CoroutineInvokeAwait(
      &_thread,
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return v1 + v2 + 1.5;
      },
      100.5, 10.5);

  double j1 = co_await coInvoke(
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return v1 + v2 + 2.5;
      },
      100.5, 15.5);

  double j2 = co_await coInvoke(
      &_thread,
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return v1 + v2 + 3.5;
      },
      100.5, 15.5);

  Example _e;

  double k1 = co_await coInvoke(&Example::tst_double, &_e, 10, 10.5);
  double k2 = co_await coInvoke(&_thread, &Example::tst_double, &_e, 10, 11.5);

  co_await coInvoke(&Example::tst_void, &_e, std::string {"string1"});
  std::string str {"string2"};
  co_await coInvoke(&_thread, &Example::tst_void, &_e, str);

  lsNotice() << i << j1 << j2 << k1 << k2 << str << AsyncFw::Thread::current()->name();

  MainThread::exit();
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineAwaitExamplePool");

  task();

  lsNotice() << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton " << ret;
  return ret;
}
