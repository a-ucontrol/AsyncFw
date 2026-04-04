/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <core/DataArray.h>
#include <core/AbstractTlsSocket.h>
#include <core/LogStream.h>
#include <ListenSocket.h>
#include <MainThread.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static TcpSocket *create() { return new TcpSocket; }
  void stateEvent() {
    stateChanged(state_);
    logDebug() << "State event:" << static_cast<int>(state_);
  }
  void readEvent() { received(read()); }
  AsyncFw::FunctionConnectorProtected<TcpSocket>::Connector<const AsyncFw::DataArray &> received;
  AsyncFw::FunctionConnectorProtected<TcpSocket>::Connector<AsyncFw::AbstractSocket::State> stateChanged;
};

int main(int argc, char *argv[]) {
  AsyncFw::ListenSocket ls;
  ls.incoming([](int fd, const std::string &address, bool *accept) {
    TcpSocket *socket = TcpSocket::create();
    socket->setDescriptor(fd);
    socket->received([socket](const AsyncFw::DataArray &data) {
      logNotice() << "received:" << data;
      socket->write("Answer\n");
      if (data == AsyncFw::DataArray('q')) AsyncFw::MainThread::exit(0);
    });
    logInfo() << "Incoming:" << fd << address;
    *accept = true;
  });

  ls.listen("0.0.0.0", 18080);

  if (argc == 2 && std::string(argv[1]) == "--tst") {
    TcpSocket *_socket = TcpSocket::create();
    _socket->stateChanged([_socket](const AsyncFw::AbstractSocket::State state) {
      if (state == AsyncFw::AbstractSocket::State::Active) {
        logDebug() << "Send request";
        _socket->write("q");
      }
    });
    _socket->connect("127.0.0.1", 18080);
  }

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
