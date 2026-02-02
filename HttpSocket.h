#pragma once

#include "core/FunctionConnector.h"
#include "core/DataArray.h"
#include "core/TlsSocket.h"

namespace AsyncFw {
class HttpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static HttpSocket *create(AsyncFw::Thread *_t = nullptr);

  void stateEvent() override;
  void activateEvent() override;
  void readEvent() override;
  void writeEvent() override;

  AsyncFw::DataArrayView header();
  AsyncFw::DataArrayView content();
  void clear();

  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::State> stateChanged;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::DataArray &> received;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::Error> error;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<> writeContent;

protected:
  HttpSocket();
  ~HttpSocket();

private:
  AsyncFw::DataArray received_;
  std::size_t contentLenght_;
  int headerSize_ = -1;
  int tid_ = -1;
};
}  // namespace AsyncFw
