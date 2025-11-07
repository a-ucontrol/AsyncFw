#pragma once

#include "DataArraySocket.h"
#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
class DataArrayTcpClient : public DataArrayAbstractTcp {
protected:
  class Thread : public DataArrayAbstractTcp::Thread {
    friend class DataArrayTcpClient;

  public:
    inline DataArrayTcpClient *client() { return static_cast<DataArrayTcpClient *>(pool); }

  private:
    using DataArrayAbstractTcp::Thread::Thread;
    ~Thread() override;
    DataArraySocket *createSocket();
    void removeSocket(DataArraySocket *socket);
  };
  virtual void socketStateChanged(const DataArraySocket *);

public:
  enum Result { ErrorExchangeNotActive = -103, ErrorExchangeTransmit = -104, ErrorExchangeConnectionClose = -105, ErrorExchangeTimeout = -106, ErrorExchangeThread = -107 };
  DataArrayTcpClient(const std::string & = "TcpClient", AbstractThread * = nullptr);
  void connectToHost(DataArraySocket *socket, const std::string &, uint16_t, int = 0);
  void connectToHost(const DataArraySocket *, int = 0);
  Thread *createThread() { return new Thread(name() + " thread", this); }
  DataArraySocket *createSocket(Thread *thread = nullptr);
  void removeSocket(DataArraySocket *);
  FunctionConnectorProtected<DataArrayTcpClient>::Connector<const DataArraySocket *> connectionStateChanged;
  int exchange(const DataArraySocket *, const DataArray &, const DataArray *, uint32_t, int = 5000);
  std::size_t socketLimit() { return maxSockets; }
  int connectTimeout() const { return waitForConnectTimeoutInterval; }
  void setConnectTimeout(int timeout) { waitForConnectTimeoutInterval = timeout; }
  int reconnectTimeout() const { return reconnectTimeoutInterval; }
  void setReconnectTimeout(int timeout) { reconnectTimeoutInterval = timeout; }

private:
  int waitForConnectTimeoutInterval = 10000;
  int reconnectTimeoutInterval = 10000;
};
}  // namespace AsyncFw
