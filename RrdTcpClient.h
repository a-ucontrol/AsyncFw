#pragma once

#include "core/FunctionConnector.h"
#include "core/TlsContext.h"

namespace AsyncFw {
class Rrd;
class DataArraySocket;
class DataArrayTcpClient;

class RrdTcpClient {
public:
  RrdTcpClient(DataArraySocket *, const std::vector<Rrd *> &);
  ~RrdTcpClient();
  void clear();

  void connectToHost(const std::string &, uint16_t);
  void connectToHost();
  void disconnectFromHost();
  int transmit(const DataArray &, uint32_t, bool = false);
  void clearHost();

  void tlsSetup(const TlsContext &);
  void disableTls();

private:
  std::vector<Rrd *> rrd_;
  DataArraySocket *tcpSocket;
  DataArrayTcpClient *tcpClient;
  int requestTimerId;
  uint64_t lastTime;
  void tcpReadWrite(const DataArray *, uint32_t);
  void connectionStateChanged();
  void request(int = 0);
  uint32_t dbSize;
  FunctionConnectionGuardList gl_;
};
}  // namespace AsyncFw
