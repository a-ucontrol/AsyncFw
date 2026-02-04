#pragma once

#include "core/FunctionConnector.h"
#include "core/DataArray.h"
#include "core/TlsSocket.h"
#include "File.h"

namespace AsyncFw {
class HttpSocket : public AsyncFw::AbstractTlsSocket {
public:
  friend LogStream &operator<<(LogStream &, const HttpSocket &);

  static HttpSocket *create(AsyncFw::Thread *_t = nullptr);

  void stateEvent() override;
  void activateEvent() override;
  void readEvent() override;
  void writeEvent() override;

  void disconnect() override;
  void close() override;
  void destroy() override;

  AsyncFw::DataArrayView header();
  AsyncFw::DataArrayView content();
  void sendFile(const std::string &);

  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::State> stateChanged;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::DataArray &> received;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<> writeContent;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<int> progress;

protected:
  HttpSocket();
  ~HttpSocket();
  void clearReceived();
  bool connectionClose = false;

private:
  AsyncFw::DataArray received_;
  AsyncFw::File file_;
  int progress_;
  int headerSize_;
  std::size_t contentLenght_ = std::string::npos;
  int tid_ = -1;
  bool full_ = false;
};
}  // namespace AsyncFw
