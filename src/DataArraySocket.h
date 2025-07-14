#pragma once

#include <deque>
#include "core/TlsSocket.h"
#include "core/TlsContext.h"
#include "core/Thread.h"
#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket : public AbstractTlsSocket {
  friend class DataArrayTcpClient;

public:
  class ReceiveFilter {
    friend class DataArraySocket;
    virtual bool received(const DataArray *, uint32_t) = 0;

  protected:
    virtual ~ReceiveFilter();
  };
  DataArraySocket(SocketThread * = nullptr);
  ~DataArraySocket() override;

  bool initTls(const TlsContext &data);
  void disableTls();

  void disconnectFromHost();
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
    maxReadSize    = size;
  }
  void setWriteBuffers(int buffers, int size) {
    maxWriteBuffers = buffers;
    maxWriteSize    = size;
  }
  void initServerConnection();
  void setHost(const std::string &address, uint16_t port) const {
    hostAddress_v = address;
    hostPort_v    = port;
  }
  const std::string hostAddress() const { return hostAddress_v; }
  uint16_t hostPort() const { return hostPort_v; }
  void installReceiveFilter(ReceiveFilter *);
  void removeReceiveFilter(ReceiveFilter *);
  void transmitKeepAlive() { transmitKeepAlive(true); }
  std::vector<ReceiveFilter *> filters() const { return receiveFilters; }

  void setStateChanged(std::function<void(AbstractSocket::State)> _stateChanged) { stateChanged = _stateChanged; }
  void setReceived(std::function<void(const DataArray *, uint32_t)> _received) { received = _received; }

  mutable FunctionConnectorProtected<DataArraySocket>::Connector<> disconnected;

protected:
  virtual bool receiveData(DataArray *, uint32_t *) { return true; }
  virtual bool transmitData(DataArray *, uint32_t *) { return true; }

  void timerEvent() override;
  void stateEvent() override;
  void readEvent() override;
  void errorEvent() override;

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
  void clearBuffer_(const DataArray *) const;
  void connectToHost();
  void connectToHost(int timeout);
  void writeSocket();
  void sendMessage(const std::string &, uint8_t);
  void transmitKeepAlive(bool);
  std::string peerString() const;
  std::vector<ReceiveFilter *> receiveFilters;
  std::function<void(AbstractSocket::State)> stateChanged;
  std::function<void(const DataArray *, uint32_t)> received;

  AsyncFw::ExecLoopThread::Holder *wait_holder_ = nullptr;  //!!! Private
};
}  // namespace AsyncFw
