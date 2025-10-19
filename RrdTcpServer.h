#pragma once

#include <queue>
#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Rrd;

class RrdTcpServer {
public:
  RrdTcpServer(DataArrayTcpServer *, const std::vector<Rrd *> &Rrd);
  virtual ~RrdTcpServer();
  void quit();

protected:
  void transmit(const DataArraySocket *socket, uint64_t index, uint32_t size, uint32_t pi);
  DataArrayTcpServer *tcpServer;
  std::queue<const DataArraySocket *> sockets;
  std::vector<Rrd *> rrd;
  FunctionConnectionGuard rf_;
};
}  // namespace AsyncFw
