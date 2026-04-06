/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/HttpServer>
#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

int main(int argc, char *argv[]) {
  HttpServer _http {HTTP_SERVER_HOME};

  _http.addRoute("/quit", HttpServer::Request::Method::Get, [](const HttpServer::Request &request) {
    HttpServer::Response *resp = request.response();
    resp->setMimeType("octet-stream");
    resp->setStatusCode(HttpServer::Response::StatusCode::Ok);
    resp->addHeader("ContentLenght:0");
    resp->send();
    MainThread::exit();
  });

  _http.listen(18080);

  if (argc == 2 && std::string(argv[1]) == "--tst") {
    HttpSocket *_socket = HttpSocket::create();
    _socket->stateChanged([_socket](const AsyncFw::AbstractSocket::State state) {
      if (state == AsyncFw::AbstractSocket::State::Active) {
        logDebug() << "Send request";
        _socket->write("GET / HTTP/1.1\r\n\r\n");
      }
    });
    _socket->received([_socket](const AsyncFw::DataArray &_da) {
      lsDebug() << _da.view(0, _da.size() > 1024 ? 1024 : _da.size());
      _socket->write("GET /quit HTTP/1.1\r\n\r\n");
    });
    _socket->connect("127.0.0.1", 18080);
  }

  lsNotice() << "Start Applicaiton";
  int ret = MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
