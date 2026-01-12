#pragma once

#include "core/TlsContext.h"
#include "DataArrayAbstractTcp.h"

namespace AsyncFw {
class ListenSocket;
class DataArrayTcpServer : public DataArrayAbstractTcp {
public:
  DataArrayTcpServer(const std::string & = "TcpServer", AsyncFw::Thread * = nullptr);
  void quit() override;
  bool listen(const std::string &address, uint16_t port);
  void close();
  void setAlwaysConnect(const std::vector<std::string> &list);
  bool listening();
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
