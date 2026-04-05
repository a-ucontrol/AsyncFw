/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/AbstractTlsSocket>
#include <AsyncFw/TlsContext>
#include <AsyncFw/AddressInfo>
#include <AsyncFw/File>
#include <AsyncFw/HttpSocket>
#include <AsyncFw/Log>

#define SERVER_NAME "github.com"
#define SERVER_PORT 443
#define GET_FILE "/a-ucontrol/AsyncFw"

int main(int argc, char *argv[]) {
  AsyncFw::TlsContext context;

  if (!context.setDefaultVerifyPaths()) {
    logError("Can't set default verify paths");
    return -1;
  }
  context.setVerifyName(SERVER_NAME);

  AsyncFw::Thread _t("HttpSocketThread");
  _t.start();
  AsyncFw::HttpSocket *socket = AsyncFw::HttpSocket::create(&_t);

  socket->setContext(context);

  AsyncFw::AddressInfo addressInfo;
  addressInfo.setTimeout(30000);
  addressInfo.resolve(SERVER_NAME);
  addressInfo.completed([&socket](int r, const std::vector<std::string> &list) {
    if (r == 0 && !list.empty()) {
      for (const std::string _s : list) logNotice() << _s;
      socket->connect(list[0], SERVER_PORT);
      return;
    }
    AsyncFw::MainThread::exit(-1);
  });

  socket->stateChanged([&socket](const AsyncFw::AbstractSocket::State state) {
    if (state == AsyncFw::AbstractSocket::State::Active) {
      logDebug() << "Send request";
      socket->write("GET " GET_FILE " HTTP/1.1\r\nHost:" SERVER_NAME "\r\n\r\n");
    } else if (state == AsyncFw::AbstractSocket::State::Unconnected) {
      if (socket->errorString().empty()) logInfo() << "Unconnected";
      else { logError() << socket->errorString(); }
      AsyncFw::MainThread::exit(0);
    }
  });
  socket->received([socket](const AsyncFw::DataArray &answer) {
    logDebug() << socket->header();
    logDebug() << socket->content();
    logNotice() << answer.view(0, 4096);
    socket->disconnect();
  });

  logNotice() << "Start Applicaiton";

  logDebug() << *AsyncFw::AbstractThread::currentThread();
  logDebug() << *(AsyncFw::AbstractSocket *)socket;

  int ret = AsyncFw::MainThread::exec();

  logDebug() << *(AsyncFw::AbstractSocket *)socket;

  logNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
