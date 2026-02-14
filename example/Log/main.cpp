#include <Version.h>
#include <MainThread.h>
#include <Log.h>

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::Log>::create(1000, "log-file-name");
  AsyncFw::Log::instance()->setColorOut(true);

  logNotice() << "Version:" << AsyncFw::Version::str();
  logNotice() << "Git:" << AsyncFw::Version::git();

  lsTrace() << "Trace";
  lsDebug() << "Debug";
  lsInfo() << "Info";
  lsInfoRed() << "Info red";
  lsInfoGreen() << "Info green";
  lsInfoBlue() << "Info blue";
  lsInfoMagenta() << "Info magenta";
  lsInfoCyan() << "Info cyan";
  lsNotice() << "Notice";
  lsWarning() << "Warning";
  lsError() << "Error";

  logTrace() << "Trace";
  logDebug() << "Debug";
  logInfo() << "Info";
  logNotice() << "Notice";
  logWarning() << "Warning";
  logError() << "Error";
  logAlert() << "logAlert";
  //logEmergency() << "Emergency";  // throw

  AsyncFw::Thread::currentThread()->invokeMethod([](){AsyncFw::MainThread::exit();});

  int ret = AsyncFw::MainThread::exec();
  return ret;
}
