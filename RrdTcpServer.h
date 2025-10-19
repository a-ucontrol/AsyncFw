#pragma once

#include <queue>
#include "core/DataArray.h"
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
  virtual void rrdRead(uint64_t index, int size);
  void transmit(uint64_t index, uint64_t lastIndex, const DataArrayList &list, bool wait = false);
  DataArrayTcpServer *tcpServer;
  std::queue<const DataArraySocket *> sockets;
  std::vector<Rrd *> rrd;
  FunctionConnectionGuard rf_;

private:
  int rrdIndex;
};
}  // namespace AsyncFw
