#include "DataArrayTcpServer.h"
#include "Rrd.h"
#include "core/LogStream.h"
#include "RrdTcpServer.h"

#ifdef EXTEND_LOG_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

#define TRANSMIT_COUNT 100

using namespace AsyncFw;
RrdTcpServer::RrdTcpServer(DataArrayTcpServer *_tcpServer, const std::vector<Rrd *> &_rrd) : tcpServer(_tcpServer), rrd(_rrd) {
  rf_ = tcpServer->received([this](const DataArraySocket *socket, const DataArray *da, uint32_t pi) {
    int v = pi & 0x0F;
    if (v > rrd.size()) {
      ucError() << "failed rrd index";
      return;
    }

    uint64_t from = *(uint64_t *)(da->data());
    sockets.push(socket);
    rrdRead(from, TRANSMIT_COUNT);
  });
  ucTrace();
}

RrdTcpServer::~RrdTcpServer() { ucTrace(); }

void RrdTcpServer::quit() { tcpServer->quit(); }

void RrdTcpServer::rrdRead(uint64_t index, int size) {
  uint64_t lastIndex;
  DataArrayList _v;
  uint64_t i = rrd[0]->read(&_v, index, size, &lastIndex);

  logAlert() << index << _v.size();

  transmit(i, lastIndex, _v);
}

void RrdTcpServer::transmit(uint64_t index, uint64_t lastIndex, const DataArrayList &list, bool wait) {
  DataStream _ds;
  _ds << index;
  _ds << list;
  _ds << lastIndex;

  DataArray _da = DataArray::compress(_ds.array());

  trace() << lastIndex << list.size();
  tcpServer->transmit(sockets.front(), _da, 0, wait);
  sockets.pop();
}
