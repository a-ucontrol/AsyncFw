#include <MainThread.h>
#include <core/TlsSocket.h>
#include <core/TlsContext.h>
#include <AddressInfo.h>
#include <File.h>
#include <Log.h>

#include "../common/HttpSocket/HttpSocket.hpp"

#define SERVER_NAME "github.com"
#define SERVER_PORT 443
#define GET_FILE    "/a-ucontrol/AsyncFw"

int main(int argc, char *argv[]) {
  AsyncFw::LogMinimal log;

  AsyncFw::TlsContext context;

  if (!context.setDefaultVerifyPaths()) {
    logError("Can't set default verify paths");
    return -1;
  }
  context.setVerifyName(SERVER_NAME);

  AsyncFw::Thread _t("HttpSocketThread");
  _t.start();
  HttpSocket *socket = HttpSocket::create(&_t);

  socket->setContext(context);
#if 1
  AsyncFw::AddressInfo addressInfo;
  addressInfo.setTimeout(30000);
  addressInfo.resolve(SERVER_NAME);
  addressInfo.completed([&socket](int r, const std::vector<std::string> &list) {
    if (r == 0 && !list.empty()) {
      for (const std::string _s : list) logNotice() << _s;
      socket->connect(list[0], SERVER_PORT);
      return;
    }
    AsyncFw::MainThread::instance()->exit(0);
  });
#else
  socket.connect("192.168.110.100", SERVER_PORT);
#endif
  socket->stateChanged([&socket](const AsyncFw::AbstractSocket::State state) {
    if (state == AsyncFw::AbstractSocket::State::Active) {
      logDebug() << "Send request";
      //socket->write("GET " GET_FILE " HTTP/1.1\r\nHost:" SERVER_NAME "\r\nConnection:close\r\n\r\n");
      socket->write("GET " GET_FILE " HTTP/1.1\r\nHost:" SERVER_NAME "\r\n\r\n");
    }
  });
  socket->received([](const AsyncFw::DataArray &header, const AsyncFw::DataArray &content) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    logNotice() << header.view(0, 512);
    logDebug() << content.view(0, 1024);
    AsyncFw::MainThread::instance()->exit(0);
  });
  socket->error([&socket](const AsyncFw::AbstractSocket::Error) {
    logError() << "Error!";
    AsyncFw::MainThread::instance()->exit(0);
  });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::instance()->exec();

  logNotice() << "End Applicaiton";

  return ret;
}
