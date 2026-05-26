/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <iostream>
#include <AsyncFw/Version>
#include <AsyncFw/MainThread>
#include <AsyncFw/Log>
#include <AsyncFw/RrdServer>
#include <AsyncFw/RrdClient>
#include <AsyncFw/DataArrayTcpClient>

int main(int argc, char *argv[]) {
  AsyncFw::Thread logThread {"LogThread"};
  logThread.start();
  logThread.invoke([]() { AsyncFw::Instance<AsyncFw::Log>::create(1000); }, true);

  AsyncFw::DataArrayTcpServer _tcp_server;
  AsyncFw::RrdServer _rrd_server {&_tcp_server, {AsyncFw::Log::instance()}};
  _tcp_server.listen("0.0.0.0", 10000);

  AsyncFw::Log _log {1000};
  AsyncFw::DataArrayTcpClient _tcp_client;
  AsyncFw::DataArraySocket *_client_socket = _tcp_client.createSocket();
  AsyncFw::RrdClient _rrd_client {_client_socket, {&_log}};
  _rrd_client.connectToHost("127.0.0.1", 10000);

  uint64_t index = _log.lastIndex();
  AsyncFw::FunctionConnectionGuard _g;
  _g = _log.updated.connect([&_log, &index, &_g]() {
    if (index == _log.lastIndex()) return;
    AsyncFw::Rrd::ItemList _list;
    _log.read(&_list, index, std::numeric_limits<uint32_t>::max(), &index);
    std::cout << "=====" << index << "=====" << _log.lastIndex() << "=====";
    for (const AsyncFw::Rrd::Item &_item : _list) {
      AsyncFw::LogStream::Message _message = _log.messageFromRrdItem(_item);
      std::cout << "-----" << _message.string << "-----" << std::endl;
      if (_message.string == "logAlert") {
        std::cout << "----- AsyncFw::MainThread::exit() -----" << std::endl;
        _g = {};
        AsyncFw::MainThread::exit();
        break;
      }
    }
  }, AsyncFw::AbstractFunctionConnector::Connection::Direct);

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
  //logEmergency() << "Emergency";  // throw

  lsNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
