#include "DataArraySocket.h"
#include "DataArrayTcpServer.h"
#include "Rrd.h"
#include "core/LogStream.h"
#include "RrdServer.h"

#ifdef EXTEND_RRD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

#define TRANSMIT_COUNT 100

using namespace AsyncFw;
RrdServer::RrdServer(DataArrayTcpServer *_tcpServer, const std::vector<Rrd *> &_rrd) : tcpServer(_tcpServer), rrd(_rrd) {
  rf_ = tcpServer->received([this](const DataArraySocket *socket, const DataArray *da, uint32_t pi) {
    if (pi >= rrd.size()) {
      trace() << "failed rrd index" << LogStream::Color::Red << pi;
      return;
    }
    transmit(socket, *reinterpret_cast<const uint64_t *>(da->data()), TRANSMIT_COUNT, pi);
  });
  lsTrace();
}

RrdServer::~RrdServer() { lsTrace(); }

void RrdServer::quit() { tcpServer->quit(); }

void RrdServer::transmit(const DataArraySocket *socket, uint64_t index, uint32_t size, uint32_t pi) {
  uint64_t lastIndex;
  DataArrayList _list;
  uint64_t i = rrd[pi]->read(&_list, index, size, &lastIndex);

  DataStream _ds;
  _ds << i;
  _ds << _list;
  _ds << lastIndex;

  DataArray _da = DataArray::compress(_ds.array());

  trace() << index << i << lastIndex << _list.size() << LogStream::Color::Red << pi;
  tcpServer->transmit(socket, _da, pi);
}
