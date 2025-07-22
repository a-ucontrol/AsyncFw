#include "DataArrayTcpClient.h"
#include "Log.h"
#include "LogTcpClient.h"

#ifdef uC_LOGGER
using namespace AsyncFw;
LogTcpClient::LogTcpClient(DataArrayTcpClient *client, int size, const std::string &file, DataArraySocket *socket) {
  log_ = new Log(size, file.data(), true);
  lastTime = log_->lastIndex();
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

  requestTimerId = log_->appendTimerTask(0, [this]() {
    log_->modifyTimer(requestTimerId, 0);
    request();
  });
}

LogTcpClient::~LogTcpClient() {
  log_->removeTimer(requestTimerId);
  tcpClient->removeSocket(tcpSocket);
  delete log_;
  ucTrace();
}

void LogTcpClient::clear() {
  log_->clear();
  lastTime = 0;
  log_->invokeMethod([this]() { log_->updated(); });
}

void LogTcpClient::connectToHost(const std::string &address, uint16_t port) { tcpClient->connectToHost(tcpSocket, address, port, 0); }

void LogTcpClient::connectToHost() { tcpClient->connectToHost(tcpSocket, 0); }

void LogTcpClient::disconnectFromHost() { tcpClient->disconnectFromHost(tcpSocket); }

int LogTcpClient::transmit(const DataArray &ba, uint32_t pi, bool wait) { return tcpClient->transmit(tcpSocket, ba, pi, wait); }

void LogTcpClient::tlsSetup(const TlsContext &data) { tcpClient->initTls(tcpSocket, data); }

void LogTcpClient::disableTls() { tcpSocket->disableTls(); }

void LogTcpClient::clearHost() { tcpSocket->setHost("", 0); }

void LogTcpClient::tcpReadWrite(const DataArray *rba, uint32_t pi) {
  if (pi == 2) request(3);
  if (pi != 0) return;
  log_->invokeMethod([this, rba(*rba)]() {
    DataArray _da = DataArray::uncompress(rba);
    if (_da.empty()) {
      logError() << "Error read log";
      return;
    }
    DataStream _ds(_da);
    uint32_t val;
    DataArrayList list;
    uint32_t dbLastTime;
    _ds >> val;
    _ds >> list;
    _ds >> dbLastTime;

    if (_ds.fail()) {
      logError() << "Error read message list";
      return;
    }

    uint32_t li = log_->lastIndex();
    if (li && (val < lastTime || val - lastTime > static_cast<unsigned int>(dbSize))) {
      log_->clear();
      log_->updated();
      lastTime = (dbLastTime > dbSize) ? dbLastTime - dbSize : 0;
      request();
      return;
    }
    if (list.size() > 0) {
      for (std::size_t i = 0; i != list.size(); ++i) li = log_->Rrd::append(list[i], val - list.size() + i + 1);

      lastTime = li;
      if (val != dbLastTime) {
        request();
        return;
      }
    }
    tcpSocket->thread()->modifyTimer(requestTimerId, 1000);
    return;

    tcpSocket->thread()->modifyTimer(requestTimerId, 1000);
  });
}

void LogTcpClient::request(int pi) {
  DataArray ba(1, '\x01');
  ba.resize(5);
  *reinterpret_cast<uint32_t *>(ba.data() + 1) = lastTime + 1;
  tcpSocket->transmit(ba, pi);
}

void LogTcpClient::connectionStateChanged() {
  if (tcpSocket->state() == AbstractSocket::Active) request();
  else
    tcpSocket->thread()->modifyTimer(requestTimerId, 0);
}
#endif
