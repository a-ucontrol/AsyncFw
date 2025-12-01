#pragma once

#include <core/FunctionConnector.h>
#include <core/TlsSocket.h>

class HttpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static HttpSocket *create(AsyncFw::Thread *_t = nullptr) { return new HttpSocket(_t); }

  void stateEvent() override;
  void readEvent() override;

  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::State> stateChanged;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::DataArray &, const AsyncFw::DataArray &> received;
  AsyncFw::FunctionConnectorProtected<HttpSocket>::Connector<const AsyncFw::AbstractSocket::Error> error;

protected:
  HttpSocket(AsyncFw::Thread *t) : AbstractTlsSocket(t) {}
  ~HttpSocket() override = default;

private:
  AsyncFw::DataArray header_;
  AsyncFw::DataArray content_;
  std::size_t contentLenght_;
};
