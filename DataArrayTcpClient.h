#pragma once

#include "DataArraySocket.h"
#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
class DataArrayTcpClient : public DataArrayAbstractTcp {
protected:
  class TcpExchange : public DataArraySocket::ReceiveFilter {
    friend class DataArrayTcpClient;

  public:
    TcpExchange(DataArraySocket *);
    int exchange(const DataArray &, DataArray *, uint32_t, int);
    int wait(int = 0);
    void wake(int = ErrorExchangeWait);
    bool isWait() { return result == -1; }

  private:
    bool received(const DataArray *, uint32_t) override;
    DataArray *exchangeByteArray;
    uint32_t exchangeId;
    DataArraySocket *socket;
    std::mutex mutex;
    std::condition_variable waitCondition;
    int result;
  };
  class Thread : public DataArrayAbstractTcp::Thread {
    friend class DataArrayTcpClient;

  public:
    inline DataArrayTcpClient *client() { return static_cast<DataArrayTcpClient *>(pool); }

  private:
    using DataArrayAbstractTcp::Thread::Thread;
    ~Thread() override;
    void destroy() override;
    DataArraySocket *createSocket();
    void removeSocket(DataArraySocket *socket);
    void removeFilters(DataArraySocket *socket);
  };
  virtual void socketStateChanged(const DataArraySocket *);

public:
  enum Result { ErrorExchangeNotConnected = -103, ErrorExchangeTransmit = -104, ErrorExchangeWait = -105, ErrorExchangeTimeout = -106, ErrorExchangeFromSocketThread = -107 };
  DataArrayTcpClient(SocketThread * = nullptr, bool init = true);
  void connectToHost(DataArraySocket *socket, const std::string &, uint16_t, int = 0);
  void connectToHost(const DataArraySocket *, int = 0);
  Thread *createThread() { return new Thread(this); }
  DataArraySocket *createSocket(Thread *thread = nullptr);
  void removeSocket(DataArraySocket *);
  int exchange(const DataArraySocket *socket, const DataArray &wba, DataArray *rba, uint32_t pi, int timeout = 10000) { return static_cast<TcpExchange *>(socket->filters().at(0))->exchange(wba, rba, pi, timeout); }
  FunctionConnectorProtected<DataArrayTcpClient>::Connector<const DataArraySocket *> connectionStateChanged;
};
}  // namespace AsyncFw
