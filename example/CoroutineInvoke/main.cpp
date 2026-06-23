/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/Coroutine>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

struct TST {
  TST(int i) : val(i) { lsInfoGreen() << val; }
  ~TST() { lsInfoRed() << val; }
  TST(const TST &v) {
    val = v.val;
    lsInfoMagenta() << "copy" << val;
  }
  TST(TST &&v) {
    val = v.val;
    v.val = -1;
    lsInfoCyan() << "move" << val;
  }
  int val = 0;
};

class Example {
public:
  double tst_double(int v1, double v2) const {
    std::chrono::milliseconds(10);
    lsDebug() << v1 << v2 << Thread::current()->name();
    return v1 + v2 + 10000.0;
  }
  void tst_void(const std::string &s) const {
    std::chrono::milliseconds(10);
    lsDebug() << s << Thread::current()->name();
  }
  std::string tst_string(std::string &s) const {
    std::chrono::milliseconds(10);
    lsDebug() << s << Thread::current()->name();
    s = "-" + s + "-";
    return "***";
  }
};

CoroutineTask task() {
  Thread _thread = Thread("CoroutineInvokeAwait");
  _thread.start();

  const Example _e;
  double i1 = co_await coInvoke(&Example::tst_double, &_e, 10, 10.5);
  double i2 = co_await coInvoke(&_thread, &Example::tst_double, &_e, 10, 11.5);
  co_await coInvoke(&Example::tst_void, &_e, std::string {"string1"});
  std::string _s {"string2"};
  co_await coInvoke(&_thread, &Example::tst_void, &_e, _s);
  std::string s = co_await coInvoke(&_thread, &Example::tst_string, &_e, _s);

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

  double j3;
  TST _tst{1001};
  {  //lambda scope
    auto lambda = [](double v1, double v2, TST) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      lsNotice() << "lambda" << AsyncFw::Thread::current()->name();
      return v1 + v2 + 30.5;
    };
    j3 = co_await coInvoke(&_thread, lambda, 100.5, 15.5, _tst);
    j3 += co_await coInvoke(&_thread, lambda, 100.5, 15.5, std::move(_tst));
  }

  lsNotice() << i1 << i2 << j1 << j2 << j3 << s << _s << AsyncFw::Thread::current()->name();

  MainThread::exit();
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineAwaitExamplePool");

  task();

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application " << ret;
  return ret;
}
