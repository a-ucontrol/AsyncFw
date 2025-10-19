#include "DataArrayTcpClient.h"
#include "Log.h"
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
RrdTcpClient::RrdTcpClient(DataArrayTcpClient *client, int size, const std::string &file, DataArraySocket *socket) {
  rrd_ = new Log(size, file.data(), true);  //!!! need use std::vector<Rrd*>
  lastTime = rrd_->lastIndex();
  tcpClient = client;
  if (!socket) {
    tcpSocket = tcpClient->createSocket();
    tcpSocket->setConnectTimeout(1000);
    tcpSocket->setReconnectTimeout(1000);
  } else
    tcpSocket = socket;

  dbSize = size;

  gl_ += tcpClient->received([this](const DataArraySocket *socket, const DataArray *da, uint32_t pi) {
    if (socket == tcpSocket) tcpReadWrite(da, pi);
  });

  gl_ += tcpClient->connectionStateChanged([this](const DataArraySocket *socket) {
    if (socket == tcpSocket) connectionStateChanged();
  });

  requestTimerId = rrd_->appendTimerTask(0, [this]() {
    rrd_->modifyTimer(requestTimerId, 0);
    request();
  });
}

RrdTcpClient::~RrdTcpClient() {
  rrd_->removeTimer(requestTimerId);
  delete rrd_;
  tcpSocket->stateChanged(
      [c = tcpClient, s = tcpSocket](AbstractSocket::State state) {
        if (state != AbstractSocket::Unconnected) return;
        c->removeSocket(s);
      },
      AbstractThread::currentThread());
  disconnectFromHost();
  ucTrace();
}

void RrdTcpClient::clear() {
  rrd_->clear();
  lastTime = 0;
  rrd_->invokeMethod([this]() { rrd_->updated(); });
}

void RrdTcpClient::connectToHost(const std::string &address, uint16_t port) { tcpClient->connectToHost(tcpSocket, address, port, 0); }

void RrdTcpClient::connectToHost() { tcpClient->connectToHost(tcpSocket, 0); }

void RrdTcpClient::disconnectFromHost() { tcpClient->disconnectFromHost(tcpSocket); }

int RrdTcpClient::transmit(const DataArray &ba, uint32_t pi, bool wait) { return tcpClient->transmit(tcpSocket, ba, pi, wait); }

void RrdTcpClient::tlsSetup(const TlsContext &data) { tcpClient->initTls(tcpSocket, data); }

void RrdTcpClient::disableTls() { tcpSocket->disableTls(); }

void RrdTcpClient::clearHost() { tcpSocket->setHost("", 0); }

void RrdTcpClient::tcpReadWrite(const DataArray *rba, uint32_t pi) {
  if (pi == 2) request(3);
  if (pi != 0) return;
  rrd_->invokeMethod([this, rba(*rba)]() {
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

    uint64_t li = rrd_->lastIndex();
    if (li && (val < lastTime || val - lastTime > static_cast<unsigned int>(dbSize))) {
      rrd_->clear();
      rrd_->updated();
      lastTime = (dbLastTime > dbSize) ? dbLastTime - dbSize : 0;
      request();
      return;
    }
    if (list.size() > 0) {
      for (std::size_t i = 0; i != list.size(); ++i) li = rrd_->Rrd::append(list[i], val - list.size() + i + 1);

      lastTime = li;
      if (val != dbLastTime) {
        request();
        return;
      }
    }
    rrd_->modifyTimer(requestTimerId, 1000);
  });
}

void RrdTcpClient::request(int pi) {
  DataArray ba;
  ba.resize(8);
  *reinterpret_cast<uint64_t *>(ba.data() + 1) = lastTime + 1;
  tcpSocket->transmit(ba, pi);
  trace() << lastTime;
}

void RrdTcpClient::connectionStateChanged() {
  if (tcpSocket->state() == AbstractSocket::Active) request();
  else { rrd_->modifyTimer(requestTimerId, 0); }
}
