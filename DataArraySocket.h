#pragma once

#include <deque>
#include "core/TlsSocket.h"
#include "core/TlsContext.h"
#include "core/Thread.h"
#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket : public AbstractTlsSocket {
  friend class DataArrayAbstractTcp;
  friend class DataArrayTcpClient;

public:
  DataArraySocket(SocketThread * = nullptr);
  ~DataArraySocket() override;

  bool initTls(const TlsContext &data);
  void disableTls();

  bool transmit(const DataArray &, uint32_t, bool = false) const;
  void clearBuffer(const DataArray *) const;
  int connectTimeout() const { return waitForConnectTimeoutInterval; }
  void setConnectTimeout(int timeout) { waitForConnectTimeoutInterval = timeout; }
  int reconnectTimeout() const { return reconnectTimeoutInterval; }
  void setReconnectTimeout(int timeout) { reconnectTimeoutInterval = timeout; }
  void setReadTimeout(int timeout) { readTimeoutInterval = timeout; }
  void setWaitForEncryptedTimeout(int timeout) { waitForEncryptedTimeoutInterval = timeout; }
  void setWaitKeepAliveAnswerTimeout(int timeout) { ((waitKeepAliveAnswerTimeoutInterval = timeout) > 0) ? waitTimerType |= 0x80 : waitTimerType &= ~0x80; }
  void setReadBuffers(int buffers, int size) {
    maxReadBuffers = buffers;
    maxReadSize = size;
  }
  void setWriteBuffers(int buffers, int size) {
    maxWriteBuffers = buffers;
    maxWriteSize = size;
  }
  void initServerConnection();
  void setHost(const std::string &address, uint16_t port) const {
    hostAddress_v = address;
    hostPort_v = port;
  }
  const std::string hostAddress() const { return hostAddress_v; }
  uint16_t hostPort() const { return hostPort_v; }
  void transmitKeepAlive() { transmitKeepAlive(true); }

  bool connect(const std::string &, uint16_t) override;
  void disconnect() override;

  mutable FunctionConnectorProtected<DataArraySocket>::Connector<AbstractSocket::State> stateChanged {AbstractFunctionConnector::DirectOnly};
  mutable FunctionConnectorProtected<DataArraySocket>::Connector<const DataArray *, uint32_t> received {AbstractFunctionConnector::DirectOnly};

protected:
  void stateEvent() override;
  void readEvent() override;
  void errorEvent() override;
  using AbstractTlsSocket::connect;

private:
  int sslConnection;
  DataArray *receiveByteArray;
  uint8_t waitTimerType;
  int maxReadBuffers;
  int maxReadSize;
  int maxWriteBuffers;
  int maxWriteSize;
  int waitForConnectTimeoutInterval;
  int reconnectTimeoutInterval;
  int readTimeoutInterval;
  int waitForEncryptedTimeoutInterval;
  int waitKeepAliveAnswerTimeoutInterval;
  int timerId;
  uint32_t readSize;
  uint32_t readId;
  mutable bool waitForTransmit;
  mutable std::condition_variable waitCondition;
  mutable std::vector<DataArray *> receiveList;
  mutable std::string address;
  mutable uint16_t port;
  mutable std::string hostAddress_v;
  mutable uint16_t hostPort_v;
  mutable std::deque<DataArray> transmitList;
  bool connectToHost();
  bool connectToHost(int timeout);
  void clearBuffer_(const DataArray *) const;
  void writeSocket();
  void sendMessage(const std::string &, uint8_t);
  void transmitKeepAlive(bool);
  std::string peerString() const;
  void startTimer(int _ms);
  void removeTimer();
  void timerEvent();
  int tid_ = -1;
  AsyncFw::ExecLoopThread::Holder *wait_holder_ = nullptr;
};
}  // namespace AsyncFw
