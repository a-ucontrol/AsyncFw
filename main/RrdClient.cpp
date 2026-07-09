/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "core/LogStream.h"
#include "core/Thread.h"
#include "DataArraySocket.h"
#include "Rrd.h"
#include "RrdClient.h"

#ifdef EXTEND_RRD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace() \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;
RrdClient::RrdClient(DataArraySocket *socket, const std::vector<Rrd *> &rrd) : rrd_(rrd), tcpSocket(socket), lastTime(rrd.size(), 0) {
  gl_ += socket->received.connect([this](const DataArray *da, uint32_t pi) {
    if (pi > 0x0F) {
      lsError() << "(pi > 0x0F)" << pi;
      return;
    }
    tcpRead(da, pi);
  });

  gl_ += socket->stateChanged.connect([this](AbstractSocket::State _state) {
    if (_state == AbstractSocket::State::Destroy) {
      tcpSocket = nullptr;
      return;
    }
    if (_state == AbstractSocket::State::Active)
      for (std::size_t i = 0; i != rrd_.size(); ++i) request(i);
    else { tcpSocket->thread()->modifyTimer(requestTimerId, 0); }
  });

  requestTimerId = tcpSocket->thread()->appendTimerTask(0, [this]() {
    tcpSocket->thread()->modifyTimer(requestTimerId, 0);
    for (std::size_t i = 0; i != rrd_.size(); ++i) request(i);
  });
  if (tcpSocket->state() == AbstractSocket::State::Active)
    for (std::size_t i = 0; i != rrd_.size(); ++i) request(i);
  lsTrace();
}

RrdClient::~RrdClient() {
  if (tcpSocket && tcpSocket->thread()) tcpSocket->thread()->removeTimer(requestTimerId);
  lsTrace();
}

void RrdClient::connectToHost(const std::string &address, uint16_t port) {
  tcpSocket->thread()->invoke([this, address, port]() { tcpSocket->connect(address, port); });
}

void RrdClient::connectToHost() {
  tcpSocket->thread()->invoke([this]() { tcpSocket->connect(tcpSocket->hostAddress(), tcpSocket->hostPort()); });
}

void RrdClient::disconnectFromHost() {
  tcpSocket->thread()->invoke([this]() { tcpSocket->disconnect(); });
}

bool RrdClient::transmit(const DataArray &da, uint32_t pi, bool wait) { return tcpSocket->transmit(da, pi, wait); }

void RrdClient::setTlsContext(const TlsContext &context) { tcpSocket->setContext(context); }

void RrdClient::tcpRead(const DataArray *rda, uint32_t n) {
  DataArray _da = DataArray::uncompress(*rda);
  if (_da.empty()) {
    lsError() << "error read log";
    return;
  }
  DataStream _ds(_da);
  uint64_t val;
  DataArrayList list;
  uint64_t dbLastTime;
  _ds >> val;
  _ds >> list;
  _ds >> dbLastTime;

  if (_ds.fail()) {
    lsError() << "error read message list";
    return;
  }

  uint32_t dbSize = rrd_[n]->size();
  if (val < lastTime[n] || val - lastTime[n] > dbSize) {
    rrd_[n]->clear();
    lastTime[n] = (dbLastTime > dbSize) ? dbLastTime - dbSize : 0;
    request(n);
    return;
  }
  if (list.size() > 0) {
    for (std::size_t i = 0; i != list.size(); ++i) rrd_[n]->append(list[i], val - list.size() + i + 1);

    lastTime[n] = val;
    if (val != dbLastTime) {
      request(n);
      return;
    }
  }
  tcpSocket->thread()->modifyTimer(requestTimerId, 1000);
}

void RrdClient::request(int n) {
  DataArray ba;
  ba.resize(8);
  *reinterpret_cast<uint64_t *>(ba.data()) = lastTime[n] + 1;
  tcpSocket->transmit(ba, n);
  trace() << lastTime[n];
}
