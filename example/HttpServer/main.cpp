#define uC_LOGGER

//! [snippet]
#include <csignal>
#include <HttpServer.h>
#include <MainThread.h>
#include <core/LogStream.h>

using namespace AsyncFw;

void handleSignal(int _sig) { MainThread::exit(); }

int main(int argc, char *argv[]) {
  signal(SIGINT, handleSignal);
  signal(SIGTERM, handleSignal);

  logNotice() << "Start";

  HttpServer _http{HTTP_SERVER_HOME};
  _http.listen(18080);

  lsNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
