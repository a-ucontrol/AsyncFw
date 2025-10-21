#pragma once

#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Rrd;

class RrdServer {
public:
  RrdServer(DataArrayTcpServer *, const std::vector<Rrd *> &Rrd);
  virtual ~RrdServer();
  void quit();

protected:
  void transmit(const DataArraySocket *socket, uint64_t index, uint32_t size, uint32_t pi);
  DataArrayTcpServer *tcpServer;
  std::vector<Rrd *> rrd;
  FunctionConnectionGuard rf_;
};
}  // namespace AsyncFw
