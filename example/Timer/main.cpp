/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <iostream>
#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>

int main(int argc, char *argv[]) {
  int cnt = 0;
  AsyncFw::Timer timer1;
  timer1.start(100);
  AsyncFw::Timer timer2;
  timer2.start(200);

  timer1.timeout([&cnt]() {
    std::cout << std::chrono::system_clock::now() << " timer1 timeout" << std::endl;
    if (++cnt == 10) AsyncFw::MainThread::exit(0);
  });

  timer2.timeout([]() { std::cout << std::chrono::system_clock::now() << " timer2 timeout" << std::endl; });

  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
//! [snippet]
