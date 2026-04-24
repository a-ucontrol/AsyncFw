/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/ApplicationNotifier>
#include <AsyncFw/LogStream>

enum AppMessages { MainThreadStarted, MainThreadFinished, TestThreadStarted, TestThreadFinished };

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ApplicationNotifier>::create();

  AsyncFw::Thread _test;

  AsyncFw::ApplicationNotifier::instance()->notify([&_test](const AsyncFw::ApplicationNotifier::Value &_val) {
    lsNotice() << _val.type;
    if (!_val.empty()) lsInfoMagenta() << _val.data<std::chrono::time_point<std::chrono::system_clock>>();
    if (_val.type == TestThreadStarted) _test.quit();
    else if (_val.type == TestThreadFinished) { AsyncFw::MainThread::exit(); }
  });

  _test.started([]() { AsyncFw::ApplicationNotifier::instance()->notify({TestThreadStarted, std::chrono::system_clock::now()}); });
  _test.finished([]() { AsyncFw::ApplicationNotifier::instance()->notify({TestThreadFinished, std::chrono::system_clock::now()}); });
  _test.start();

  AsyncFw::Thread::currentThread()->started([]() { AsyncFw::ApplicationNotifier::instance()->notify({MainThreadStarted}); });
  AsyncFw::Thread::currentThread()->finished([]() { AsyncFw::ApplicationNotifier::instance()->notify({MainThreadFinished}); });

  lsNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;
  return ret;
}
//! [snippet]
