/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/Thread>
#include <AsyncFw/AbstractTlsSocket>
#include <AsyncFw/TlsContext>
#include <AsyncFw/LogStream>
#include <AsyncFw/AddressInfo>
#include <AsyncFw/MainThread>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  void stateEvent() {
    logDebug() << "State event:" << static_cast<int>(state_);
    if (state_ == Active) {
      logDebug() << "Send request";
      write("GET /a-ucontrol/AsyncFw HTTP/1.1\r\nHost:github.com\r\nConnection:close\r\n\r\n");
    } else if (error() >= Closed) {
      if (error() != Closed) logError() << errorString();
      else {
        logNotice() << "Received:" << da;
        logDebug() << da.view(0, 512) << "...";
      }
      if (h) {
        h.promise().resume_queued();
        h = {};
        return;
      }
      AsyncFw::MainThread::exit(0);
    }
  }
  void readEvent() {
    AsyncFw::DataArray _da = read();
    logTrace() << "Read event:" << _da.size() << std::endl << _da.view(0, 256);
    da += _da;
  }

  AsyncFw::CoroutineAwait coConnect(const std::string &address, uint16_t port) {
    return AsyncFw::CoroutineAwait([this, address, port](AsyncFw::CoroutineHandle _h) {
      h = _h;
      connect(address, port);
    });
  }

private:
  AsyncFw::CoroutineHandle h;
  AsyncFw::DataArray da;
};

int main(int argc, char *argv[]) {
  AsyncFw::TlsContext context;

  if (!context.setDefaultVerifyPaths()) {
    logError("Can't set default verify paths");
    return -1;
  }
  context.setVerifyName("github.com");
  TcpSocket socket;
  socket.setContext(context);

  auto coroTask {[&socket]() -> AsyncFw::CoroutineTask {
    AsyncFw::AddressInfo addressInfo;
    AsyncFw::CoroutineHandle h = co_await addressInfo.coResolve("github.com");
    AsyncFw::AddressInfo::Result list = h.promise().data<AsyncFw::AddressInfo::Result>();
    if (list.empty()) {
      logError("Resolve error");
      AsyncFw::MainThread::exit(-1);
      co_return;
    }
    for (const std::string _s : list) logNotice() << _s;
    co_await socket.coConnect(list[0], 443);
    AsyncFw::MainThread::exit(0);
  }};
  coroTask();

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton" << ret;
  return ret;
}
//! [snippet]
