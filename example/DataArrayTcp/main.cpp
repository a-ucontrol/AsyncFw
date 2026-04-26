/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/DataArrayTcpServer>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  AsyncFw::DataArrayTcpServer _server;
  AsyncFw::DataArrayTcpClient _client;
  AsyncFw::DataArraySocket *_socket;

  _server.received([](const AsyncFw::DataArraySocket *socket, const AsyncFw::DataArray *data, uint32_t id) {
    lsDebug() << "Server received" << *socket << *data << id;
    socket->transmit("Ok", id);
  });
  _client.received([](const AsyncFw::DataArraySocket *socket, const AsyncFw::DataArray *data, uint32_t id) {
    lsDebug() << "Client received" << *socket << *data << id;
    if (id == 9) AsyncFw::MainThread::exit();
  });

  _client.connectionStateChanged([](const AsyncFw::DataArraySocket *socket) {
    lsDebug() << *socket;
    if (socket->state() == AsyncFw::AbstractSocket::Active) {
      for (int i = 0; i != 10; ++i) socket->transmit("Client transmit", i);
    }
  });

  _server.listen("0.0.0.0", 18080);
  _socket = _client.createSocket();
  _client.connectToHost(_socket, "127.0.0.1", 18080);

  lsNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;
  return ret;
}
//! [snippet]
