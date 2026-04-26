/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <map>
#include <memory>
#include <functional>

#include "../core/TlsContext.h"
#include "HttpSocket.h"
#include "Instance.h"

class WebSocket;
namespace AsyncFw {
using namespace AsyncFw;
/*! \class HttpServer HttpServer.h <AsyncFw/HttpServer> \brief The HttpServer class.
 \brief Example: \snippet HttpServer/main.cpp snippet */
class HttpServer {
  friend LogStream &operator<<(LogStream &, const HttpServer &);
  struct Private;

public:
  class Response;

protected:
  class TcpSocket : public AsyncFw::HttpSocket {
    friend class HttpServer;

  public:
    TcpSocket(HttpServer *);
    ~TcpSocket();

  protected:
    void readEvent() override;

  private:
    Response *response = nullptr;
    WebSocket *ws_ = nullptr;
    HttpServer *server_;
  };

public:
  class Response {
    friend class HttpServer;

  public:
    enum class StatusCode {
      Continue = 100,
      SwitchingProtocols = 101,
      Processing = 102,
      Ok = 200,
      Created = 201,
      Accepted = 202,
      NonAuthoritativeInformation = 203,
      NoContent = 204,
      ResetContent = 205,
      PartialContent = 206,
      MultiStatus = 207,
      AlreadyReported = 208,
      IMUsed = 226,
      MultipleChoices = 300,
      MovedPermanently = 301,
      Found = 302,
      SeeOther = 303,
      NotModified = 304,
      UseProxy = 305,
      TemporaryRedirect = 307,
      PermanentRedirect = 308,
      BadRequest = 400,
      Unauthorized = 401,
      PaymentRequired = 402,
      Forbidden = 403,
      NotFound = 404,
      MethodNotAllowed = 405,
      NotAcceptable = 406,
      ProxyAuthenticationRequired = 407,
      RequestTimeout = 408,
      Conflict = 409,
      Gone = 410,
      LengthRequired = 411,
      PreconditionFailed = 412,
      PayloadTooLarge = 413,
      UriTooLong = 414,
      UnsupportedMediaType = 415,
      RequestRangeNotSatisfiable = 416,
      ExpectationFailed = 417,
      ImATeapot = 418,
      MisdirectedRequest = 421,
      UnprocessableEntity = 422,
      Locked = 423,
      FailedDependency = 424,
      UpgradeRequired = 426,
      PreconditionRequired = 428,
      TooManyRequests = 429,
      RequestHeaderFieldsTooLarge = 431,
      LoginTimeOut = 440,
      UnavailableForLegalReasons = 451,
      InternalServerError = 500,
      NotImplemented = 501,
      BadGateway = 502,
      ServiceUnavailable = 503,
      GatewayTimeout = 504,
      HttpVersionNotSupported = 505,
      VariantAlsoNegotiates = 506,
      InsufficientStorage = 507,
      LoopDetected = 508,
      NotExtended = 510,
      NetworkAuthenticationRequired = 511,
      NetworkConnectTimeoutError = 599
    };

    void setMimeType(const std::string &mime) { mimeType_ = mime; }
    std::string mimeType() const { return mimeType_; }
    void addHeader(const std::string &ba) { additionalHeaders.push_back(ba); }
    std::string header() const;

    void destroy();
    bool send();
    void setContent(const AsyncFw::DataArray &);
    void setContent(const std::vector<uint8_t> &);
    void setStatusCode(const StatusCode &_statusCode) { statusCode_ = _statusCode; }
    bool fail() { return socket_ == nullptr; }

    Response::StatusCode statusCode() { return statusCode_; }
    AsyncFw::DataArray content() { return content_; }
    TcpSocket *socket() { return socket_; }

  private:
    std::vector<std::string> additionalHeaders;
    mutable StatusCode statusCode_ = Response::StatusCode::Ok;
    mutable int contentLength = 0;
    mutable std::string mimeType_;
    AsyncFw::DataArray content_;

    std::string version = "1.1";
    bool cors_headers_enabled = false;
    mutable TcpSocket *socket_ = nullptr;
  };

  class Request {
    friend class HttpServer;
    struct Private;

  public:
    enum class Method {
      Unknown = 0x0000,
      Get = 0x0001,
      Put = 0x0002,
      Delete = 0x0004,
      Post = 0x0008,
      Head = 0x0010,
      Options = 0x0020,
      Patch = 0x0040,
      Connect = 0x0080,
      Trace = 0x0100,
      //AnyKnown = Get | Put | Delete | Post | Head | Options | Patch | Connect | Trace,
    };

    Request(const std::string_view &);
    virtual ~Request();

    Method method() const { return method_; }
    std::string methodName() const;
    std::string path() const;
    std::string heaaderItemValue(const std::string &) const;
    std::string queryItemValue(const std::string &) const;

    AsyncFw::DataArray content() const;
    std::string peerAddress() const { return response_->socket_->peerAddress(); }

    void setSocketData(const std::any &data) const { response_->socket_->data_ = data; }
    std::any &socketData() const { return response_->socket_->data_; }
    Response *response() const { return response_; }
    bool switchingProtocols() const;

    bool fail() const;

  private:
    Method method_;
    Response *response_ = nullptr;
    Private *private_;
  };

private:
  class HttpRule {
    friend class HttpServer;

  public:
    const Request::Method method;

  private:
    HttpRule(const Request::Method method, std::function<void(const Request &)> exec) : method(method), exec(exec) {}
    std::function<void(const Request &)> exec;
  };

public:
  using RulesMap = std::multimap<std::string, std::unique_ptr<HttpRule>>;

  HttpServer(const std::string & = {});
  virtual ~HttpServer();

  template <typename A>
  void addRoute(const std::string &url, Request::Method method, A action, const std::any &data = {}) {
    addRule(url, method, [this, action, data](const Request &request) {
      if (!peek || peek(request, data)) action(request);
    });
    if (findRule(url, Request::Method::Options) != rules.end()) return;
    addRule(url, Request::Method::Options, [this, action, data](const Request &request) {
      if (cors_request_enabled) request.response()->setStatusCode(Response::StatusCode::Ok);
      else { request.response()->setStatusCode(Response::StatusCode::Forbidden); }
    });
  }
  template <typename O, typename A>
  void addRoute(const O object, const std::string &url, Request::Method method, A action, const std::any &data = {}) {
    addRoute(url, method, [object, action](const Request &request) { return (object->*action)(request); }, data);
  }

  template <typename T>
  void clearConnections(const T &_data) {
    for (TcpSocket *socket : sockets) {
      if (socket->data_.has_value() && _data == std::any_cast<T>(socket->data_)) disconnectFromHost(socket);
    }
  }
  void clearConnections() {
    for (TcpSocket *socket : sockets) disconnectFromHost(socket);
  }
  template <typename T>
  int sendToWebSockets(const T &_data, const AsyncFw::DataArray &_da) {
    AsyncFw::DataArray _f;
    int _r = 0;
    for (TcpSocket *socket : sockets) {
      if (!socket->ws_) continue;
      if (socket->data_.has_value() && _data == std::any_cast<T>(socket->data_)) {
        if (_f.empty()) {
          int _s = makeWebSocketFrame(_da, &_f);
          if (_s <= 0) return -1;
        }
        socket->write(_f);
        ++_r;
      }
    }
    return _r;
  }
  void sendToWebSockets(const std::string &);

  void disconnectFromHost(TcpSocket *socket);
  bool listen(uint16_t port);
  void close();

  uint16_t port();
  bool hasTls();

  void addRule(const std::string &, const Request::Method, std::function<void(const Request &)>);
  bool execRule(const Request &);

  void setTlsContext(const AsyncFw::TlsContext &);
  void setEnableCorsRequests(bool);
  void setPeek(std::function<bool(const Request &, const std::any &)> f) { peek = f; }

  static inline HttpServer *instance() { return instance_.value; }
  AsyncFw::FunctionConnectorProtected<HttpServer>::Connector<int, const std::string &, bool *> incoming {AsyncFw::AbstractFunctionConnector::SyncOnly};

protected:
  virtual void fileUploadProgress(TcpSocket *, int);
  RulesMap rules;

private:
  static inline Instance<HttpServer> instance_ {"HttpServer"};
  void received(TcpSocket *, const std::string_view &);
  int makeWebSocketFrame(const AsyncFw::DataArray &, AsyncFw::DataArray *);
  RulesMap::iterator findRule(const std::string &, const Request::Method);
  std::vector<TcpSocket *> sockets;
  bool cors_request_enabled = true;
  std::function<bool(const Request &, std::any)> peek;
  Private *private_;
};
}  // namespace AsyncFw
