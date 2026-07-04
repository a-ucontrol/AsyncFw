/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file HttpServer.h @brief The HttpServer class. */

#include <map>
#include <memory>

#include "../core/TlsContext.h"
#include "HttpSocket.h"
#include "Instance.h"

class WebSocket;
namespace AsyncFw {
using namespace AsyncFw;
/** @class HttpServer HttpServer.h <AsyncFw/HttpServer> @brief Asynchronous HTTP and WebSocket server.
@details The HttpServer class provides routing mechanisms, support for secure TLS connections, integration with WebSockets via frames, and a custom request filtering hook (peek).
@brief Example: @snippet HttpServer/main.cpp snippet */
class HttpServer {
  friend LogStream &operator<<(LogStream &, const HttpServer &);

public:
  class Response;

protected:
  /** @class TcpSocket @brief Internal socket class for handling low-level client HTTP/WebSocket connections. */
  class TcpSocket : public AsyncFw::HttpSocket {
    friend class HttpServer;

  public:
    /**  @brief Constructs a new TcpSocket associated with the parent server. @param server Pointer to the parent HttpServer instance. */
    TcpSocket(HttpServer *);
    ~TcpSocket();

  protected:
    /**  @brief Triggered when new raw data is available in the socket for reading. @n Overrides the base behavior to handle either incomin WebSocket frames or standard HTTP requests. */
    void readEvent() override;

  private:
    Response *response = nullptr;  ///< The current HTTP response being built for this socket.
    WebSocket *ws_ = nullptr;      ///< Pointer to the WebSocket instance if a protocol upgrade occurred.
    HttpServer *server_;           ///< Reference back to the parent HTTP server.
  };

public:
  /** @class Response @brief Class used to construct and transmit HTTP responses to clients. @n This class holds status codes, response headers, content types, and body data, allowing the application to safely serialize and send responses asynchronously. */
  class Response {
    friend class HttpServer;

  public:
    /** @brief Standard HTTP status codes (as per RFC 7231). */
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

    /** @brief Sets the MIME content type of the response. @param mime The MIME type string (e.g., "text/html", "application/json"). */
    void setMimeType(const std::string &mime) { mimeType_ = mime; }
    /** @brief Retrieves the current MIME type of the response. */
    std::string mimeType() const { return mimeType_; }
    /** @brief Appends a custom HTTP header line to the response. @param ba The fully formatted header string (e.g., "X-Custom-Header: value"). */
    void addHeader(const std::string &ba) { additionalHeaders.push_back(ba); }
    /** @brief Generates and returns the complete serialized HTTP header section. */
    std::string header() const;
    /** @brief Finalizes and sends the response over the network via the associated socket. @return True if the response was successfully dispatched. */
    bool send();
    /** @brief Sets the payload content from a raw DataArray.
    @note Can accept special local file paths ("file://...") to trigger static file streaming. */
    void setContent(const AsyncFw::DataArray &);
    /** @brief Sets the payload content from a byte vector. */
    void setContent(const std::vector<uint8_t> &);
    /** @brief Assigns the HTTP status code for this response. */
    void setStatusCode(const StatusCode &_statusCode) { statusCode_ = _statusCode; }
    /** @brief Checks if the response delivery is bound to fail due to a disconnected socket. */
    bool fail() { return socket_ == nullptr; }
    /** @brief Destroys the response object and cleans up its allocated resources. */
    void destroy();

    /** @brief Retrieves the currently assigned HTTP status code. */
    Response::StatusCode statusCode() { return statusCode_; }
    /** @brief Retrieves the current response payload body. */
    AsyncFw::DataArray content() { return content_; }
    /** @brief Returns the underlying socket through which this response is transmitted. */
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

  /** @class Request @brief Representation of an incoming client HTTP request. @n Parses raw incoming request buffers and provides programmatic access to methods, query strings, headers, parameters, and client network addresses. */
  class Request {
    friend class HttpServer;

  public:
    /** @brief Enumeration of supported HTTP request methods. */
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

    /** @brief Parses and initializes a Request instance from a raw stream string view. @param str Raw incoming HTTP header/body buffer. */
    Request(const std::string_view &);
    virtual ~Request();

    /** @brief Returns the HTTP method of the request. */
    Method method() const { return method_; }
    /** @brief Returns the string representation of the HTTP method (e.g., "GET", "POST"). */
    std::string methodName() const;
    /** @brief Returns the absolute path requested by the client (e.g., "/index.html"). */
    std::string path() const;
    /** @brief Extracts the value of a specific HTTP header. @param name The target header key (case-insensitive depending on parser rules). @return The associated header value or an empty string if missing. */
    std::string heaaderItemValue(const std::string &) const;
    /** @brief Extracts a specific key's value from the URL query string. @param name Query parameter name. @return Parameter value or an empty string. */
    std::string queryItemValue(const std::string &) const;

    /** @brief Extracts the body content payload from the incoming request. */
    AsyncFw::DataArray content() const;
    /** @brief Returns the client remote IP address. */
    std::string peerAddress() const { return response_->socket_->peerAddress(); }

    /** @brief Stores an arbitrary context/state object inside the current socket instance. @param data Any valid std::any state container. */
    void setSocketData(const std::any &data) const { response_->socket_->data_ = data; }
    /** @brief Retrieves the context state attached to the client socket. */
    std::any &socketData() const { return response_->socket_->data_; }
    /** @brief Links back to the responsive companion structure paired with this request. */
    Response *response() const { return response_; }
    /** @brief Checks if the client headers request a protocol switch (e.g., WebSocket upgrade). */
    bool switchingProtocols() const;

    /** @brief Evaluates whether the request parser flagged any structural formatting errors. */
    bool fail() const;

  private:
    Method method_;
    Response *response_ = nullptr;
    struct Private;
    Private &private_;
  };

private:
  class HttpRule {
    friend class HttpServer;

  public:
    const Request::Method method;
    ~HttpRule();
    HttpRule(HttpRule &&);

  private:
    template <typename T>
    HttpRule(const Request::Method method, T &f) : method(method), exec(new Invocable<void(const Request &)>::Function(std::forward<T>(f))) {}
    HttpRule(HttpRule &) = delete;
    Invocable<void(const Request &)>::Abstract *exec = nullptr;
  };

public:
  using RulesMap = std::multimap<std::string, std::unique_ptr<HttpRule>>;
  /** @brief Constructs the HTTP server instance. @param httpPath Optional base path for serving static assets. */
  HttpServer(const std::string & = {});
  virtual ~HttpServer();
  /** @brief Binds a free-standing function or a lambda expression as a route handler.
   @n Automatically generates a fallback handler for OPTIONS requests (CORS handling) unless it has already been explicitly declared for the target path. @tparam A Callable function wrapper type matching `void(const Request&)`. @param url The endpoint URI pattern (e.g., "/api/v1/status"). @param method The target HTTP execution method. @param action The user function invoked when the endpoint is matched. @param data Optional context variable evaluated by the global `peek` filter. */
  template <typename A>
  void addRoute(const std::string &url, Request::Method method, A action, const std::any &data = {}) {
    addRule(url, method, [this, action, data](const Request &request) {
      if (!peek || (*peek)(request, data)) action(request);
    });
    if (findRule(url, Request::Method::Options) != rules.end()) return;
    addRule(url, Request::Method::Options, [this, action, data](const Request &request) {
      if (cors_request_enabled) request.response()->setStatusCode(Response::StatusCode::Ok);
      else { request.response()->setStatusCode(Response::StatusCode::Forbidden); }
    });
  }
  /** @brief Binds a specific class instance method as a route handler. @tparam O Pointer type targeting the owning class instance. @tparam A Pointer type matching a method signature of `void Object::Method(const Request&)`. @param object Pointer to the instance executing the action. @param url The endpoint URI pattern. @param method The target HTTP execution method. @param action Pointer to the member method. @param data Optional context variable evaluated by the global `peek` filter. */
  template <typename O, typename A>
  void addRoute(const O object, const std::string &url, Request::Method method, A action, const std::any &data = {}) {
    addRoute(url, method, [object, action](const Request &request) { return (object->*action)(request); }, data);
  }
  /** @brief Broadcasts a binary array payload to active WebSocket connections matching a specific filter criteria. @n Iterates through connected channels, matching their internal `socketData` against the provided key. @tparam T Type of the verification value stored inside the socket state. @param _data Reference value used to filter recipient sockets. @param _da Raw payload data to package inside the WebSocket framing layer. @return Count of successfully messaged channels, or -1 if framing layout serialization failed. */
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

public:
  /** @brief Assigns a global interceptor/validation hook (Peek) evaluated prior to any route action execution. @n If this predicate hook evaluates to `false`, the underlying endpoint handler action execution is aborted. @tparam T Callable interceptor function wrapper type matching `bool(const Request&, std::any)`. @param f Predicate logic function. */
  template <typename T>
  void setPeek(T f) {
    peek = new Invocable<bool(const Request &, std::any)>::Function(std::forward<T>(f));
  }
  /** @brief Explicitly severs connections with active sockets holding custom user metadata matching `data`. @tparam T Type identifier matching the socket data context block. @param _data Target match signature identifying channels to be dropped. */
  template <typename T>
  void clearConnections(const T &data) {
    for (TcpSocket *socket : sockets) {
      if (socket->data_.has_value() && data == std::any_cast<T>(socket->data_)) disconnectFromHost(socket);
    }
  }
  /** @brief Forcefully terminates all active socket connections currently retained by the server. */
  void clearConnections();
  /** @brief Dispatches a pre-formatted frame envelope to an explicit WebSocket connection. */
  bool webSocketSend(const HttpSocket *, const DataArray &);
  /** @brief Broadcasts a raw string text frame across all active WebSocket communication channels. */
  void sendToWebSockets(const std::string &);
  /** @brief Disconnects a specific client socket and clears it from the internally managed pool. */
  void disconnectFromHost(TcpSocket *socket);
  /** @brief Binds to all available interfaces (0.0.0.0) and starts listening for incoming traffic on a specific TCP port. */
  bool listen(uint16_t port);
  /** @brief Stops the server listener immediately, preventing any new connections from being accepted. */
  void close();
  /** @brief Returns the local TCP port number the server listener is actively bound to. */
  uint16_t port();
  /** @brief Returns true if a TLS cryptographic security context (HTTPS) is currently active. */
  bool hasTls();
  /** @brief Sets up keypairs and root certificates to arm TLS operations (HTTPS mode). */
  void setTlsContext(const AsyncFw::TlsContext &);
  /** @brief Toggles whether automatic default responses for CORS validation logic (OPTIONS requests) are enabled globally. */
  void setEnableCorsRequests(bool);

  /** @brief Accessor to fetch the globally managed singleton instance reference of the active HttpServer. */
  static inline HttpServer *instance() { return instance_.value; }
  /** @brief Signal connector emitted whenever a fresh client socket establishes a network link. */
  AsyncFw::FunctionConnector<int, const std::string &, bool *>::Policy<AsyncFw::AbstractFunctionConnector::SyncOnly>::Protected<HttpServer> incoming;
  /** @brief Signal connector emitted when incoming validated WebSocket framed blocks are assembled for read operations. */
  AsyncFw::FunctionConnector<const HttpSocket *, const DataArray &>::Protected<HttpServer> webSocketReceived;

protected:
  virtual void fileUploadProgress(TcpSocket *, int);
  int makeWebSocketFrame(const AsyncFw::DataArray &, AsyncFw::DataArray *, bool = false);
  RulesMap rules;

private:
  template <typename T>
  void addRule(const std::string &url, const Request::Method method, T exec) {
    if (findRule(url, method) != rules.end()) return;
    HttpServer::rules.emplace(url, std::make_unique<HttpRule>(HttpServer::HttpRule(method, exec)));
  }
  void received(TcpSocket *, const std::string_view &);
  bool execRule(const Request &);
  RulesMap::iterator findRule(const std::string &, const Request::Method);
  std::vector<TcpSocket *> sockets;
  bool cors_request_enabled = true;
  static Instance<HttpServer> instance_;
  Invocable<bool(const Request &, std::any)>::Abstract *peek = nullptr;
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
