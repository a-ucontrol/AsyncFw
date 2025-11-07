#pragma once

#include "core/Socket.h"
#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
class DataArrayTcpServer : public DataArrayAbstractTcp {
public:
  DataArrayTcpServer(const std::string & = "TcpServer", SocketThread * = nullptr);
  void quit() override;
  bool listen(const std::string &address, uint16_t port) { return listener->listen(address, port); }
  void close() { listener->close(); }
  void setAlwaysConnect(const std::vector<std::string> &list) { alwaysConnect_ = list; }
  bool isListening() { return listener->port() != 0; }
  void tlsSetup(const TlsContext &data) { tlsData = data; }

private:
  class Thread : public DataArrayAbstractTcp::Thread {
    friend class DataArrayTcpServer;

  public:
    inline DataArrayTcpServer *server() { return static_cast<DataArrayTcpServer *>(pool); }

  private:
    using DataArrayAbstractTcp::Thread::Thread;
    void createSocket(int, bool);
    void removeSocket(DataArraySocket *socket);
  };
  std::unique_ptr<ListenSocket> listener;

  bool incomingConnection(int, const std::string &);
  std::vector<std::string> alwaysConnect_;
  TlsContext tlsData;
};
}  // namespace AsyncFw
