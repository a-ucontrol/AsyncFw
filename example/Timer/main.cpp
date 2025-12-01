#include <iostream>
#include <chrono>
#include <MainThread.h>
#include <Timer.h>

int main(int argc, char *argv[]) {
  int cnt = 0;
  AsyncFw::Timer timer1;
  timer1.start(1000);
  AsyncFw::Timer timer2;
  timer2.start(2000);

  timer1.timeout([&cnt]() {
    std::cout << std::chrono::system_clock::now() << " timer1 timeout" << std::endl;
    if (++cnt == 10) AsyncFw::MainThread::instance()->exit(0);
  });

  timer2.timeout([]() { std::cout << std::chrono::system_clock::now() << " timer2 timeout" << std::endl; });

  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::instance()->exec();
  return ret;
}
