#include "DataArraySocket.h"
#include "Rrd.h"
#include "core/LogStream.h"
#include "RrdTcpClient.h"

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

using namespace AsyncFw;
RrdTcpClient::RrdTcpClient(DataArraySocket *socket, const std::vector<Rrd *> &rrd) : rrd_(rrd), tcpSocket(socket) {
  gl_ += socket->received([this](const DataArray *da, uint32_t pi) { tcpReadWrite(da, pi); });

  gl_ += socket->stateChanged([this](AbstractSocket::State) { connectionStateChanged(); });

  requestTimerId = rrd_[0]->appendTimerTask(0, [this]() {
    rrd_[0]->modifyTimer(requestTimerId, 0);
    request();
  });
}

RrdTcpClient::~RrdTcpClient() {
  rrd_[0]->removeTimer(requestTimerId);
  disconnectFromHost();
  ucTrace();
}

void RrdTcpClient::clear() {
  rrd_[0]->clear();
  lastTime = 0;
  rrd_[0]->invokeMethod([this]() { rrd_[0]->updated(); });
}

void RrdTcpClient::connectToHost(const std::string &address, uint16_t port) {
  tcpSocket->thread()->invokeMethod([this, address, port]() { tcpSocket->connect(address, port); });
}

void RrdTcpClient::connectToHost() {
  tcpSocket->thread()->invokeMethod([this]() { tcpSocket->connect(tcpSocket->hostAddress(), tcpSocket->hostPort()); });
}

void RrdTcpClient::disconnectFromHost() {
  tcpSocket->thread()->invokeMethod([this]() { tcpSocket->disconnect(); });
}

int RrdTcpClient::transmit(const DataArray &ba, uint32_t pi, bool wait) { return tcpSocket->transmit(ba, pi, wait); }

void RrdTcpClient::tlsSetup(const TlsContext &data) { tcpSocket->initTls(data); }

void RrdTcpClient::disableTls() { tcpSocket->disableTls(); }

void RrdTcpClient::tcpReadWrite(const DataArray *rba, uint32_t pi) {
  if (pi == 2) request(3);
  if (pi != 0) return;
  rrd_[0]->invokeMethod([this, rba(*rba), n = (pi & 0x0F)]() {
    DataArray _da = DataArray::uncompress(rba);
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

    uint64_t li = rrd_[n]->lastIndex();
    if (li && (val < lastTime || val - lastTime > static_cast<unsigned int>(dbSize))) {
      rrd_[n]->clear();
      rrd_[n]->updated();
      lastTime = (dbLastTime > dbSize) ? dbLastTime - dbSize : 0;
      request();
      return;
    }
    if (list.size() > 0) {
      for (std::size_t i = 0; i != list.size(); ++i) li = rrd_[n]->Rrd::append(list[i], val - list.size() + i + 1);

      lastTime = li;
      if (val != dbLastTime) {
        request();
        return;
      }
    }
    rrd_[n]->modifyTimer(requestTimerId, 1000);
  });
}

void RrdTcpClient::request(int pi) {
  DataArray ba;
  ba.resize(8);
  *reinterpret_cast<uint64_t *>(ba.data()) = lastTime + 1;
  tcpSocket->transmit(ba, pi);
  trace() << lastTime;
}

void RrdTcpClient::connectionStateChanged() {
  if (tcpSocket->state() == AbstractSocket::Active) request();
  else { rrd_[0]->modifyTimer(requestTimerId, 0); }
}
