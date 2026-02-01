#pragma once

#include <core/FunctionConnector.h>
#include <core/DataArray.h>
#include <core/TlsSocket.h>

namespace AsyncFw {
class HttpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static HttpSocket *create(AsyncFw::Thread *_t = nullptr);

  void stateEvent() override;
  void readEvent() override;

  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::State> stateChanged;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::DataArray &> receivedHeader;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::DataArray &> receivedContent;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::Error> error;

protected:
  using AsyncFw::AbstractTlsSocket::AbstractTlsSocket;
  ~HttpSocket() override = default;

private:
  AsyncFw::DataArray header_;
  AsyncFw::DataArray content_;
  std::size_t contentLenght_;
};
}  // namespace AsyncFw
