# AsyncFw
Async Framework

Timer example:
```c++
#include <iostream>
#include <chrono>
#include <AsyncFw/core/Thread.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Timer.h>

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
```
PollNotifier example (Unix only):
```c++
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
```

Log example:
```c++
#include <AsyncFw/Log.h>
#include <AsyncFw/MainThread.h>

int main(int argc, char *argv[]) {
  AsyncFw::Log(1000, "log-file-name");

  logTrace() << "Trace";
  logDebug() << "Debug";
  logInfo() << "Info";
  logNotice() << "Notice";
  logWarning() << "Warning";
  logError() << "Error";
  logAlert() << "logAlert";
  logEmergency() << "logEmergency";  // terminate application

  int ret = AsyncFw::MainThread::instance()->exec();
  return ret;
}
```
