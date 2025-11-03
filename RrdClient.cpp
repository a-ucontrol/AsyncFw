#include "DataArraySocket.h"
#include "Rrd.h"
#include "core/LogStream.h"
#include "RrdClient.h"

#ifdef EXTEND_RRD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;
RrdClient::RrdClient(DataArraySocket *socket, const std::vector<Rrd *> &rrd) : rrd_(rrd), tcpSocket(socket), lastTime(rrd.size(), 0) {
  gl_ += socket->received([this](const DataArray *da, uint32_t pi) {
    if (pi > 0x0F) {
      ucError() << "(pi > 0x0F)" << pi;
      return;
    }
    tcpReadWrite(da, pi);
  });

  gl_ += socket->stateChanged([this](AbstractSocket::State) { connectionStateChanged(); });

  requestTimerId = tcpSocket->thread()->appendTimerTask(0, [this]() {
    tcpSocket->thread()->modifyTimer(requestTimerId, 0);
    for (int i = 0; i != rrd_.size(); ++i) request(i);
  });
  if (tcpSocket->state() == AbstractSocket::Active)
    for (int i = 0; i != rrd_.size(); ++i) request(i);
  ucTrace();
}

RrdClient::~RrdClient() {
  if (tcpSocket) tcpSocket->thread()->removeTimer(requestTimerId);
  ucTrace();
}

void RrdClient::clear(int n) {
  lastTime[n] = 0;
  rrd_[n]->clear();
}

void RrdClient::connectToHost(const std::string &address, uint16_t port) {
  tcpSocket->thread()->invokeMethod([this, address, port]() { tcpSocket->connect(address, port); });
}

void RrdClient::connectToHost() {
  tcpSocket->thread()->invokeMethod([this]() { tcpSocket->connect(tcpSocket->hostAddress(), tcpSocket->hostPort()); });
}

void RrdClient::disconnectFromHost() {
  tcpSocket->thread()->invokeMethod([this]() { tcpSocket->disconnect(); });
}

int RrdClient::transmit(const DataArray &ba, uint32_t pi, bool wait) { return tcpSocket->transmit(ba, pi, wait); }

void RrdClient::tlsSetup(const TlsContext &data) { tcpSocket->initTls(data); }

void RrdClient::disableTls() { tcpSocket->disableTls(); }

void RrdClient::tcpReadWrite(const DataArray *rba, uint32_t n) {
  DataArray _da = DataArray::uncompress(*rba);
  if (_da.empty()) {
    logError() << "Error read log";
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
    logError() << "Error read message list";
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
    for (std::size_t i = 0; i != list.size(); ++i) rrd_[n]->Rrd::append(list[i], val - list.size() + i + 1);

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

void RrdClient::connectionStateChanged() {
  if (tcpSocket->state() == AbstractSocket::Destroy) {
    tcpSocket = nullptr;
    return;
  }
  if (tcpSocket->state() == AbstractSocket::Active)
    for (int i = 0; i != rrd_.size(); ++i) request(i);
  else { tcpSocket->thread()->modifyTimer(requestTimerId, 0); }
}
