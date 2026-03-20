/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <core/DataArray.h>
#include <core/TlsSocket.h>
#include <core/LogStream.h>
#include <MainThread.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static TcpSocket *create() { return new TcpSocket; }
  void stateEvent() { logDebug() << "State event:" << static_cast<int>(state_); }
  void readEvent() { received(read()); }
  AsyncFw::FunctionConnectorProtected<TcpSocket>::Connector<const AsyncFw::DataArray &> received;
};

int main(int argc, char *argv[]) {
  AsyncFw::ListenSocket ls;
  ls.incoming([](int fd, const std::string &address, bool *accept) {
    TcpSocket *socket = TcpSocket::create();
    socket->setDescriptor(fd);
    socket->received([socket](const AsyncFw::DataArray &data) {
      logNotice() << "received:" << data;
      socket->write("Answer\n");
      AsyncFw::MainThread::exit(0);
    });
    logInfo() << "Incoming:" << fd << address;
    *accept = true;
  });

  ls.listen("0.0.0.0", 18080);

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
