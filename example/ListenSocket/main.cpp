/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <netinet/in.h>
#include <AsyncFw/DataArray>
#include <AsyncFw/AbstractTlsSocket>
#include <AsyncFw/ListenSocket>
#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

class Ipv6Socket : public AsyncFw::AbstractSocket {
public:
  static Ipv6Socket *create() { return new Ipv6Socket(); }
  Ipv6Socket() : AsyncFw::AbstractSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP) {}
  void stateEvent() {
    stateChanged(state_);
    logDebug() << "State event:" << static_cast<int>(state_);
  }
  void readEvent() { received(read()); }
  AsyncFw::FunctionConnector<const AsyncFw::DataArray &>::Protected<Ipv6Socket> received;
  AsyncFw::FunctionConnector<AsyncFw::AbstractSocket::State>::Protected<Ipv6Socket> stateChanged;
};

int main(int argc, char *argv[]) {
  AsyncFw::ListenSocket ls(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  ls.incoming.connect([](int fd, const std::string &address, bool *accept) {
    Ipv6Socket *socket = Ipv6Socket::create();
    socket->setDescriptor(fd);
    socket->received.connect([socket](const AsyncFw::DataArray &data) {
      logNotice() << "received:" << data;
      socket->write("Answer\n");
      if (data == AsyncFw::DataArray('q')) AsyncFw::MainThread::exit(0);
    });
    logInfo() << "Incoming:" << fd << address;
    *accept = true;
  });

  if (!ls.listen("::1", 18080)) {
    logError() << "IPv6 listen failed";
    return -1;
  }

  if (argc == 2 && std::string(argv[1]) == "--tst") {
    Ipv6Socket *_socket = Ipv6Socket::create();
    _socket->stateChanged.connect([_socket](const AsyncFw::AbstractSocket::State state) {
      if (state == AsyncFw::AbstractSocket::State::Active) {
        logDebug() << "Send request";
        _socket->write("q");
      }
    });
    _socket->connect("::1", 18080);
  }

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Application" << ret;

  return ret;
}
//! [snippet]
