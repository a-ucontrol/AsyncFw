#include "DataArrayTcpServer.h"
#include "Log.h"
#include "LogTcpServer.h"

#ifdef uC_LOGGER

  #define TRANSMIT_COUNT 100

using namespace AsyncFw;
LogTcpServer::LogTcpServer(DataArrayTcpServer *_tcpServer, Log *_log) : tcpServer(_tcpServer), log(_log) {
  rf_ = tcpServer->received([this](const DataArraySocket *socket, const DataArray *da, uint32_t pi) {
    if (pi != 0 && pi != 3) return;
    if (da->size() == 0) return;
    if (da->at(0) == 1) {
      uint32_t from = *(uint32_t *)(da->data() + 1);
      sockets.push(socket);
      logRead(from, (pi == 0) ? TRANSMIT_COUNT : 0);
    }
  });
  ucTrace();
}

LogTcpServer::~LogTcpServer() { ucTrace(); }

void LogTcpServer::quit() { tcpServer->quit(); }

void LogTcpServer::logRead(uint32_t index, int size) {
  uint32_t lastIndex;
  DataArrayList _v;
  uint32_t i = static_cast<Log *>(log)->read(&_v, index, size, &lastIndex);
  transmit(i, lastIndex, _v);
}

void LogTcpServer::transmit(uint32_t index, uint32_t lastIndex, const DataArrayList &list, bool wait) {
  DataStream _ds;
  _ds << index;
  _ds << list;
  _ds << lastIndex;

  DataArray _da = DataArray::compress(_ds.array());

  tcpServer->transmit(sockets.front(), _da, 0, wait);
  sockets.pop();
}
#endif
