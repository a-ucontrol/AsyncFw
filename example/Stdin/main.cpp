#include <iostream>
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/PollNotifier.h>

int main(int argc, char *argv[]) {
  AsyncFw::PollNotifier notifier(STDIN_FILENO);
  if (isatty(STDIN_FILENO)) {
    notifier.notify([&notifier](AsyncFw::AbstractThread::PollEvents e) {
      char buf[128];
      int r  = read(STDIN_FILENO, buf, sizeof(buf) - 1);
      buf[r] = 0;
      if (r == 2 && buf[0] == 'q') AsyncFw::MainThread::instance()->quit();
      std::cout << "stdin: " << buf;
    });
  } else {
    std::cout << "stdin is not connected to the terminal";
    return -1;
  }
  std::cout << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::instance()->exec();
  return ret;
}
