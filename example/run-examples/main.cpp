/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/Version>
#include <AsyncFw/MainThread>
#include <AsyncFw/SystemProcess>
#include <AsyncFw/Timer>
#include <AsyncFw/Log>

void run_examples() {
  std::string app;
  AsyncFw::SystemProcess process;
  std::string err;

  bool ok = true;
  process.output([](const std::string &str, bool err) {
    if (!err) logInfo() << "OUT:" << std::endl << str;
    else { logError() << "ERR:" << std::endl << str; }
  });
  process.stateChanged([&ok, &process, &err, &app](AsyncFw::SystemProcess::State _s) {
    if (_s == AsyncFw::SystemProcess::Running) return;
    if (_s != AsyncFw::SystemProcess::Finished || process.exitCode()) {
      ok = false;
      logAlert() << "========================";
      err += app + "\n";
      logAlert() << "error:" << app;
      logAlert() << "========================";
    }
  });

  app = EXAMPLES_PATH "ContainerTask/ContainerTaskExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "Coroutine/CoroutineExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "CoroutineWait/CoroutineWaitExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "Invoke/InvokeExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "Log/LogExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "ThreadPool/ThreadPoolExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "SystemProcess/SystemProcessExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "Timer/TimerExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();
  app = EXAMPLES_PATH "HttpSocket/HttpSocketExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();

  app = EXAMPLES_PATH "MulticastDns/MulticastDnsExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();

  app = EXAMPLES_PATH "FileSystemWatcher/FileSystemWatcherExample";
  logInfo() << "Start:" << app;
  process.start(app);
  process.wait();

  app = EXAMPLES_PATH "ListenSocket/ListenSocketExample";
  logInfo() << "Start:" << app;
  process.start(app, {"--tst"});
  process.wait();
  app = EXAMPLES_PATH "HttpServer/HttpServerExample";
  logInfo() << "Start:" << app;
  process.start(app, {"--tst"});
  process.wait();

  if (!ok) {
    logAlert() << "-ERROR!-";
    logAlert() << "=ERROR!=\n" << err;
    logAlert() << "-ERROR!-";
  }

  AsyncFw::MainThread::exit(ok ? 0 : -1);
}

int main(int argc, char *argv[]) {
#ifdef USE_QAPPLICATION
  int _a = 0;
  QCoreApplication app(argc, argv);
#endif

  AsyncFw::SystemProcess::exec("/bin/ls", {EXAMPLES_PATH}, [](int _r, AsyncFw::SystemProcess::State _s, const std::string &_out, const std::string &_err) {
    logNotice() << "End:" << _r << static_cast<int>(_s);
    if (!_out.empty()) logInfo() << _out;
    if (!_err.empty()) logError() << _err;
  });

  AsyncFw::AbstractThread::currentThread()->invokeMethod([]() { run_examples(); });

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "Finished Applicaiton";

  logNotice() << "Version:" << AsyncFw::Version::str();
  logNotice() << "Git:" << AsyncFw::Version::git();
  return ret;
}
