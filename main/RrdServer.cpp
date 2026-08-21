/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "DataArraySocket.h"
#include "DataArrayTcpServer.h"
#include "Rrd.h"
#include "core/LogStream.h"
#include "RrdServer.h"

#ifdef EXTEND_RRD_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

#define TRANSMIT_COUNT 100

using namespace AsyncFw;
RrdServer::RrdServer(DataArrayTcpServer *tcpServer, const std::vector<Rrd *> &rrd) : tcpServer(tcpServer), rrd_(rrd) {
  g_ = tcpServer->received.connect([this](const DataArraySocket *socket, const DataArray *da, uint32_t pi) {
    if (pi >= rrd_.size()) {
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
  uint64_t i = rrd_[pi]->read(&_list, index, size, &lastIndex);

  DataStream _ds;
  _ds << i;
  _ds << _list;
  _ds << lastIndex;

  DataArray _da = DataArray::compress(_ds.array());

  trace() << index << i << lastIndex << _list.size() << LogStream::Color::Red << pi;
  tcpServer->transmit(socket, _da, pi);
}
