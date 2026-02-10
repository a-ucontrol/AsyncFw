#include <Version.h>
#include <Log.h>
#include <MainThread.h>

int main(int argc, char *argv[]) {
  AsyncFw::Log log(1000, "log-file-name");
  log.setColorOut(true);

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
  logEmergency() << "Emergency";  // throw

  int ret = AsyncFw::MainThread::exec();
  return ret;
}
