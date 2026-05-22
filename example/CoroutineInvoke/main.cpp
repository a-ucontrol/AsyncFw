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
    lsInfoMagenta() << val;
  }
  TST(TST &&v) {
    val = v.val;
    v.val = -1;
    lsInfoCyan() << val;
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
  void tst_void(std::string &s) const {
    std::chrono::milliseconds(10);
    lsDebug() << s << Thread::current()->name();
  }
};

CoroutineTask task() {
  Thread _thread = Thread("CoroutineInvokeAwait");
  _thread.start();

  TST _tst {12345};

  double j1 = co_await coInvoke(
      [_tst](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lsInfoGreen() << _tst.val;
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
  double j4;
  double j5;
  {
    auto lambda = [](double v1, double v2) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      //lsInfoCyan() << _tst.val;
      return v1 + v2 + 30.5;
    };
    //j3 = co_await coInvoke(&_thread, lambda, 100.5, 15.5);
    //j4 = co_await coInvoke(&_thread, lambda, 100.5, 25.5);
    //j5 = co_await coInvoke(&_thread, std::move(lambda), 100.5, 25.5);
  }

  const Example _e;

  double k1 = co_await coInvoke(&Example::tst_double, &_e, 10, 10.5);
  double k2 = co_await coInvoke(&_thread, &Example::tst_double, &_e, 10, 11.5);

  co_await coInvoke(&Example::tst_void, &_e, std::string {"string1"});
  std::string str {"string2"};
  co_await coInvoke(&_thread, &Example::tst_void, &_e, str);

  lsNotice() << j1 << j2 << j3 << j4 << k1 << k2 << str << AsyncFw::Thread::current()->name() << _tst.val;

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
