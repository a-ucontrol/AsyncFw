#pragma once

#include <queue>
#include <AsyncFw/core/DataArray.h>
#include <AsyncFw/core/FunctionConnector.h>

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Log;

class LogTcpServer {
public:
  LogTcpServer(DataArrayTcpServer *, Log *);
  virtual ~LogTcpServer();
  void quit();

protected:
  virtual void logRead(uint32_t index, int size);
  void transmit(uint32_t index, uint32_t lastIndex, const DataArrayList &list, bool wait = false);
  DataArrayTcpServer *tcpServer;
  std::queue<const DataArraySocket *> sockets;
  Log *log;
  FunctionConnectionGuard rf_;
};
}  // namespace AsyncFw
