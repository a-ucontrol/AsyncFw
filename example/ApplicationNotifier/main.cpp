/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/ApplicationNotifier>
#include <AsyncFw/LogStream>

enum AppMessages { MainThreadStarted, MainThreadFinished, TestThreadStarted, TestThreadFinished };

int main(int, char **) {
  AsyncFw::Instance<AsyncFw::ApplicationNotifier>::create();

  AsyncFw::Thread _test;

  AsyncFw::ApplicationNotifier::instance()->notify.connect([&_test](const AsyncFw::ApplicationNotifier::Value &_val) {
    lsNotice() << _val.type;
    if (!_val.empty()) lsInfo() << _val.data<int>();
    if (_val.type == TestThreadStarted) _test.quit();
    else if (_val.type == TestThreadFinished) { AsyncFw::MainThread::exit(); }
  });

  _test.started.connect([]() { AsyncFw::ApplicationNotifier::instance()->notify({TestThreadStarted, 100}); });
  _test.finished.connect([]() { AsyncFw::ApplicationNotifier::instance()->notify({TestThreadFinished, 200}); });
  _test.start();

  AsyncFw::Thread::current()->started.connect([]() { AsyncFw::ApplicationNotifier::instance()->notify({MainThreadStarted}); });
  AsyncFw::Thread::current()->finished.connect([]() { AsyncFw::ApplicationNotifier::instance()->notify({MainThreadFinished}); });

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application" << ret;
  return ret;
}
//! [snippet]
