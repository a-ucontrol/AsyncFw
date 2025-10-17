#pragma once

#include <queue>
#include "core/DataArray.h"
#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Rrd;

class LogTcpServer {
public:
  LogTcpServer(DataArrayTcpServer *, Rrd *);
  virtual ~LogTcpServer();
  void quit();

protected:
  virtual void logRead(uint32_t index, int size);
  void transmit(uint32_t index, uint32_t lastIndex, const DataArrayList &list, bool wait = false);
  DataArrayTcpServer *tcpServer;
  std::queue<const DataArraySocket *> sockets;
  Rrd *log;
  FunctionConnectionGuard rf_;
};
}  // namespace AsyncFw
