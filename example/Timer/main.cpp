/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
//#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

#include <AsyncFw/MainThread>
#include <AsyncFw/Coroutine>
#include <AsyncFw/LogStream>

int heavyCalculation(int input) {
  return input * 42;
}

AsyncFw::CoroutineTask runApplicationLogic() {
  lsNotice() << "Starting application flow on Main Thread...";
  int computationResult = co_await AsyncFw::coInvoke(heavyCalculation, 10);
  lsNotice() << "Computation successfully returned: " << computationResult;
  AsyncFw::MainThread::exit(0);
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("ExampleThreadPool");
  runApplicationLogic();
  logNotice() << "Booting Main Engine Event Loop...";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}

//! [snippet]
