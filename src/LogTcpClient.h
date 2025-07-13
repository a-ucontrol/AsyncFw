#pragma once

#include <AsyncFw/core/FunctionConnector.h>
#include <AsyncFw/core/TlsContext.h>

namespace AsyncFw {
class Log;
class DataArraySocket;
class DataArrayTcpClient;

class LogTcpClient {
public:
  LogTcpClient(DataArrayTcpClient *, int, const std::string & = {}, DataArraySocket *socket = nullptr);
  ~LogTcpClient();
  void clear();
  Log *log() { return log_; }

  void connectToHost(const std::string &, uint16_t);
  void connectToHost();
  void disconnectFromHost();
  int transmit(const DataArray &, uint32_t, bool = false);
  void clearHost();

  void tlsSetup(const TlsContext &);
  void disableTls();

private:
  Log *log_;
  DataArraySocket *tcpSocket;
  DataArrayTcpClient *tcpClient;
  int requestTimerId;
  uint32_t lastTime;
  void tcpReadWrite(const DataArray *, uint32_t);
  void connectionStateChanged();
  void request(int = 0);
  uint32_t dbSize;
  FunctionConnectionGuardList gl_;
};
}  // namespace AsyncFw
