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
  enum Result { ErrorExchangeNotConnected = -103, ErrorExchangeTransmit = -104, ErrorExchangeWait = -105, ErrorExchangeTimeout = -106, ErrorExchangeFromSocketThread = -107 };
  DataArrayTcpClient(SocketThread * = nullptr, bool init = true);
  void connectToHost(DataArraySocket *socket, const std::string &, uint16_t, int = 0);
  void connectToHost(const DataArraySocket *, int = 0);
  Thread *createThread() { return new Thread(this); }
  DataArraySocket *createSocket(Thread *thread = nullptr);
  void removeSocket(DataArraySocket *);
  FunctionConnectorProtected<DataArrayTcpClient>::Connector<const DataArraySocket *> connectionStateChanged;
};
}  // namespace AsyncFw
