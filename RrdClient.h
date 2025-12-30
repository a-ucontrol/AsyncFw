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
  void clear(int);

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
  std::vector<uint64_t> lastTime;
  void tcpReadWrite(const DataArray *, uint32_t);
  void request(int);
  FunctionConnectionGuardList gl_;
};
}  // namespace AsyncFw
