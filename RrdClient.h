#pragma once

#include "core/FunctionConnector.h"
#include "core/TlsContext.h"

namespace AsyncFw {
class Rrd;
class DataArraySocket;

class RrdClient {
public:
  RrdClient(DataArraySocket *, const std::vector<Rrd *> &);
  ~RrdClient();
  void clear();

  void connectToHost(const std::string &, uint16_t);
  void connectToHost();
  void disconnectFromHost();
  int transmit(const DataArray &, uint32_t, bool = false);

  void tlsSetup(const TlsContext &);
  void disableTls();

private:
  std::vector<Rrd *> rrd_;
  DataArraySocket *tcpSocket;
  int requestTimerId;
  uint64_t lastTime = 0;
  void tcpReadWrite(const DataArray *, uint32_t);
  void connectionStateChanged();
  void request(int = 0);
  FunctionConnectionGuardList gl_;
};
}  // namespace AsyncFw
