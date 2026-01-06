#include <MainThread.h>
#include <linux/SystemProcess.h>
#include <Timer.h>
#include <core/LogStream.h>

int main(int argc, char *argv[]) {
  AsyncFw::SystemProcess process;
  process.output([](const std::string &str, bool err) {
    if (!err) logInfo() << "OUT:" << '\n' + str;
    else { logError() << "ERR:" << '\n' + str; }
  });
  process.stateChanged([](AsyncFw::SystemProcess::State _s) {
    if (_s == AsyncFw::SystemProcess::Running) return;
    AsyncFw::MainThread::exit(0);
  });

  process.start("/bin/bash");

  AsyncFw::Timer::single(500, [&process]() { process.input("echo 1234567890\n"); });
  AsyncFw::Timer::single(1000, [&process]() { process.input("_cmd_\n"); });
  AsyncFw::Timer::single(1000, [&process]() { process.input("ls /\n"); });
  AsyncFw::Timer::single(1500, [&process]() { process.input("exit\n"); });

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
