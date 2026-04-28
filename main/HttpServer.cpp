/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <filesystem>

#include "core/LogStream.h"
#include "core/Thread.h"
#include "ListenSocket.h"

#include "3rdparty/httpparser/src/httpparser/httprequestparser.h"
#include "3rdparty/httpparser/src/httpparser/request.h"
#include "3rdparty/simple-uri-parser/uri_parser.h"
#include "3rdparty/WebSocket/WebSocket/WebSocket.h"

#ifdef EXTEND_HTTP_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define trace_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define trace_if(x) \
    if constexpr (0) LogStream()
#endif

#include "HttpServer.h"

#define SOCKET_WRITE_SIZE 8192

using namespace AsyncFw;

struct HttpServer::Private {
  static inline std::map<std::string, std::string> mimeTypes {
      {"3g2", "video/3gpp2"},
      {"3gp", "video/3gpp"},
      {"3gpp", "video/3gpp"},
      {"ac", "application/pkix-attr-cert"},
      {"adp", "audio/adpcm"},
      {"ai", "application/postscript"},
      {"apng", "image/apng"},
      {"appcache", "text/cache-manifest"},
      {"asc", "application/pgp-signature"},
      {"atom", "application/atom+xml"},
      {"atomcat", "application/atomcat+xml"},
      {"atomsvc", "application/atomsvc+xml"},
      {"au", "audio/basic"},
      {"aw", "application/applixware"},
      {"bdoc", "application/bdoc"},
      {"bmp", "image/bmp"},
      {"ccxml", "application/ccxml+xml"},
      {"cdmia", "application/cdmi-capability"},
      {"cdmic", "application/cdmi-container"},
      {"cdmid", "application/cdmi-domain"},
      {"cdmio", "application/cdmi-object"},
      {"cdmiq", "application/cdmi-queue"},
      {"cer", "application/pkix-cert"},
      {"cgm", "image/cgm"},
      {"class", "application/java-vm"},
      {"coffee", "text/coffeescript"},
      {"conf", "text/plain"},
      {"cpt", "application/mac-compactpro"},
      {"crl", "application/pkix-crl"},
      {"css", "text/css"},
      {"csv", "text/csv"},
      {"cu", "application/cu-seeme"},
      {"davmount", "application/davmount+xml"},
      {"dbk", "application/docbook+xml"},
      {"def", "text/plain"},
      {"disposition-notification", "message/disposition-notification"},
      {"doc", "application/msword"},
      {"dot", "application/msword"},
      {"drle", "image/dicom-rle"},
      {"dssc", "application/dssc+der"},
      {"dtd", "application/xml-dtd"},
      {"ear", "application/java-archive"},
      {"ecma", "application/ecmascript"},
      {"emf", "image/emf"},
      {"eml", "message/rfc822"},
      {"emma", "application/emma+xml"},
      {"eps", "application/postscript"},
      {"epub", "application/epub+zip"},
      {"es", "application/ecmascript"},
      {"exi", "application/exi"},
      {"exr", "image/aces"},
      {"ez", "application/andrew-inset"},
      {"fits", "image/fits"},
      {"g3", "image/g3fax"},
      {"gbr", "application/rpki-ghostbusters"},
      {"geojson", "application/geo+json"},
      {"gif", "image/gif"},
      {"glb", "model/gltf-binary"},
      {"gltf", "model/gltf+json"},
      {"gml", "application/gml+xml"},
      {"gpx", "application/gpx+xml"},
      {"gram", "application/srgs"},
      {"grxml", "application/srgs+xml"},
      {"gxf", "application/gxf"},
      {"gz", "application/gzip"},
      {"h261", "video/h261"},
      {"h263", "video/h263"},
      {"h264", "video/h264"},
      {"heic", "image/heic"},
      {"heics", "image/heic-sequence"},
      {"heif", "image/heif"},
      {"heifs", "image/heif-sequence"},
      {"hjson", "application/hjson"},
      {"hlp", "application/winhlp"},
      {"hqx", "application/mac-binhex40"},
      {"htm", "text/html"},
      {"html", "text/html"},
      {"ics", "text/calendar"},
      {"ief", "image/ief"},
      {"ifb", "text/calendar"},
      {"iges", "model/iges"},
      {"igs", "model/iges"},
      {"in", "text/plain"},
      {"ini", "text/plain"},
      {"ink", "application/inkml+xml"},
      {"inkml", "application/inkml+xml"},
      {"ipfix", "application/ipfix"},
      {"jade", "text/jade"},
      {"jar", "application/java-archive"},
      {"jls", "image/jls"},
      {"jp2", "image/jp2"},
      {"jpe", "image/jpeg"},
      {"jpeg", "image/jpeg"},
      {"jpf", "image/jpx"},
      {"jpg", "image/jpeg"},
      {"jpg2", "image/jp2"},
      {"jpgm", "video/jpm"},
      {"jpgv", "video/jpeg"},
      {"jpm", "image/jpm"},
      {"jpx", "image/jpx"},
      {"js", "application/javascript"},
      {"json", "application/json"},
      {"json5", "application/json5"},
      {"jsonld", "application/ld+json"},
      {"jsonml", "application/jsonml+json"},
      {"jsx", "text/jsx"},
      {"kar", "audio/midi"},
      {"ktx", "image/ktx"},
      {"less", "text/less"},
      {"list", "text/plain"},
      {"litcoffee", "text/coffeescript"},
      {"log", "text/plain"},
      {"lostxml", "application/lost+xml"},
      {"m1v", "video/mpeg"},
      {"m21", "application/mp21"},
      {"m2a", "audio/mpeg"},
      {"m2v", "video/mpeg"},
      {"m3a", "audio/mpeg"},
      {"m4a", "audio/mp4"},
      {"m4p", "application/mp4"},
      {"ma", "application/mathematica"},
      {"mads", "application/mads+xml"},
      {"man", "text/troff"},
      {"manifest", "text/cache-manifest"},
      {"map", "application/json"},
      {"markdown", "text/markdown"},
      {"mathml", "application/mathml+xml"},
      {"mb", "application/mathematica"},
      {"mbox", "application/mbox"},
      {"md", "text/markdown"},
      {"me", "text/troff"},
      {"mesh", "model/mesh"},
      {"meta4", "application/metalink4+xml"},
      {"metalink", "application/metalink+xml"},
      {"mets", "application/mets+xml"},
      {"mft", "application/rpki-manifest"},
      {"mid", "audio/midi"},
      {"midi", "audio/midi"},
      {"mime", "message/rfc822"},
      {"mj2", "video/mj2"},
      {"mjp2", "video/mj2"},
      {"mjs", "application/javascript"},
      {"mml", "text/mathml"},
      {"mods", "application/mods+xml"},
      {"mov", "video/quicktime"},
      {"mp2", "audio/mpeg"},
      {"mp21", "application/mp21"},
      {"mp2a", "audio/mpeg"},
      {"mp3", "audio/mpeg"},
      {"mp4", "video/mp4"},
      {"mp4a", "audio/mp4"},
      {"mp4s", "application/mp4"},
      {"mp4v", "video/mp4"},
      {"mpd", "application/dash+xml"},
      {"mpe", "video/mpeg"},
      {"mpeg", "video/mpeg"},
      {"mpg", "video/mpeg"},
      {"mpg4", "video/mp4"},
      {"mpga", "audio/mpeg"},
      {"mrc", "application/marc"},
      {"mrcx", "application/marcxml+xml"},
      {"ms", "text/troff"},
      {"mscml", "application/mediaservercontrol+xml"},
      {"msh", "model/mesh"},
      {"mxf", "application/mxf"},
      {"mxml", "application/xv+xml"},
      {"n3", "text/n3"},
      {"nb", "application/mathematica"},
      {"oda", "application/oda"},
      {"oga", "audio/ogg"},
      {"ogg", "audio/ogg"},
      {"ogv", "video/ogg"},
      {"ogx", "application/ogg"},
      {"omdoc", "application/omdoc+xml"},
      {"onepkg", "application/onenote"},
      {"onetmp", "application/onenote"},
      {"onetoc", "application/onenote"},
      {"onetoc2", "application/onenote"},
      {"opf", "application/oebps-package+xml"},
      {"otf", "font/otf"},
      {"owl", "application/rdf+xml"},
      {"oxps", "application/oxps"},
      {"p10", "application/pkcs10"},
      {"p7c", "application/pkcs7-mime"},
      {"p7m", "application/pkcs7-mime"},
      {"p7s", "application/pkcs7-signature"},
      {"p8", "application/pkcs8"},
      {"pdf", "application/pdf"},
      {"pfr", "application/font-tdpfr"},
      {"pgp", "application/pgp-encrypted"},
      {"pki", "application/pkixcmp"},
      {"pkipath", "application/pkix-pkipath"},
      {"pls", "application/pls+xml"},
      {"png", "image/png"},
      {"prf", "application/pics-rules"},
      {"ps", "application/postscript"},
      {"pskcxml", "application/pskc+xml"},
      {"qt", "video/quicktime"},
      {"raml", "application/raml+yaml"},
      {"rdf", "application/rdf+xml"},
      {"rif", "application/reginfo+xml"},
      {"rl", "application/resource-lists+xml"},
      {"rld", "application/resource-lists-diff+xml"},
      {"rmi", "audio/midi"},
      {"rnc", "application/relax-ng-compact-syntax"},
      {"rng", "application/xml"},
      {"roa", "application/rpki-roa"},
      {"roff", "text/troff"},
      {"rq", "application/sparql-query"},
      {"rs", "application/rls-services+xml"},
      {"rsd", "application/rsd+xml"},
      {"rss", "application/rss+xml"},
      {"rtf", "application/rtf"},
      {"rtx", "text/richtext"},
      {"s3m", "audio/s3m"},
      {"sbml", "application/sbml+xml"},
      {"scq", "application/scvp-cv-request"},
      {"scs", "application/scvp-cv-response"},
      {"sdp", "application/sdp"},
      {"ser", "application/java-serialized-object"},
      {"setpay", "application/set-payment-initiation"},
      {"setreg", "application/set-registration-initiation"},
      {"sgi", "image/sgi"},
      {"sgm", "text/sgml"},
      {"sgml", "text/sgml"},
      {"shex", "text/shex"},
      {"shf", "application/shf+xml"},
      {"shtml", "text/html"},
      {"sig", "application/pgp-signature"},
      {"sil", "audio/silk"},
      {"silo", "model/mesh"},
      {"slim", "text/slim"},
      {"slm", "text/slim"},
      {"smi", "application/smil+xml"},
      {"smil", "application/smil+xml"},
      {"snd", "audio/basic"},
      {"spp", "application/scvp-vp-response"},
      {"spq", "application/scvp-vp-request"},
      {"spx", "audio/ogg"},
      {"sru", "application/sru+xml"},
      {"srx", "application/sparql-results+xml"},
      {"ssdl", "application/ssdl+xml"},
      {"ssml", "application/ssml+xml"},
      {"stk", "application/hyperstudio"},
      {"styl", "text/stylus"},
      {"stylus", "text/stylus"},
      {"svg", "image/svg+xml"},
      {"svgz", "image/svg+xml"},
      {"t", "text/troff"},
      {"t38", "image/t38"},
      {"tei", "application/tei+xml"},
      {"teicorpus", "application/tei+xml"},
      {"text", "text/plain"},
      {"tfi", "application/thraud+xml"},
      {"tfx", "image/tiff-fx"},
      {"tif", "image/tiff"},
      {"tiff", "image/tiff"},
      {"tr", "text/troff"},
      {"ts", "video/mp2t"},
      {"tsd", "application/timestamped-data"},
      {"tsv", "text/tab-separated-values"},
      {"ttc", "font/collection"},
      {"ttf", "font/ttf"},
      {"ttl", "text/turtle"},
      {"txt", "text/plain"},
      {"u8dsn", "message/global-delivery-status"},
      {"u8hdr", "message/global-headers"},
      {"u8mdn", "message/global-disposition-notification"},
      {"u8msg", "message/global"},
      {"uri", "text/uri-list"},
      {"uris", "text/uri-list"},
      {"urls", "text/uri-list"},
      {"vcard", "text/vcard"},
      {"vrml", "model/vrml"},
      {"vtt", "text/vtt"},
      {"vxml", "application/voicexml+xml"},
      {"war", "application/java-archive"},
      {"wasm", "application/wasm"},
      {"wav", "audio/wav"},
      {"weba", "audio/webm"},
      {"webm", "video/webm"},
      {"webmanifest", "application/manifest+json"},
      {"webp", "image/webp"},
      {"wgt", "application/widget"},
      {"wmf", "image/wmf"},
      {"woff", "font/woff"},
      {"woff2", "font/woff2"},
      {"wrl", "model/vrml"},
      {"wsdl", "application/wsdl+xml"},
      {"wspolicy", "application/wspolicy+xml"},
      {"x3d", "model/x3d+xml"},
      {"x3db", "model/x3d+binary"},
      {"x3dbz", "model/x3d+binary"},
      {"x3dv", "model/x3d+vrml"},
      {"x3dvz", "model/x3d+vrml"},
      {"x3dz", "model/x3d+xml"},
      {"xaml", "application/xaml+xml"},
      {"xdf", "application/xcap-diff+xml"},
      {"xdssc", "application/dssc+xml"},
      {"xenc", "application/xenc+xml"},
      {"xer", "application/patch-ops-error+xml"},
      {"xht", "application/xhtml+xml"},
      {"xhtml", "application/xhtml+xml"},
      {"xhvml", "application/xv+xml"},
      {"xm", "audio/xm"},
      {"xml", "application/xml"},
      {"xop", "application/xop+xml"},
      {"xpl", "application/xproc+xml"},
      {"xsd", "application/xml"},
      {"xsl", "application/xml"},
      {"xslt", "application/xslt+xml"},
      {"xspf", "application/xspf+xml"},
      {"xvm", "application/xv+xml"},
      {"xvml", "application/xv+xml"},
      {"yaml", "text/yaml"},
      {"yang", "application/yang"},
      {"yin", "application/yin+xml"},
      {"yml", "text/yaml"},
      {"zip", "application/zip"},
  };

  std::string httpPath = "/www";
  AsyncFw::TlsContext tlsContext_;
  AsyncFw::ListenSocket listener;
  FunctionConnectionGuard listenerGuard;
};

struct HttpServer::Request::Private {
  friend class HttpServer;
  Private(const std::string_view &str) {
    httpparser::HttpRequestParser parser;
    res = parser.parse(request, str.data(), str.data() + str.size());
    uri = std::make_unique<uri::Uri>(uri::parse_uri(".://" + request.uri));
    req_ = str;
  }
  httpparser::Request request;
  std::unique_ptr<uri::Uri> uri;
  httpparser::HttpRequestParser::ParseResult res;

  std::string req_;
};

HttpServer::TcpSocket::TcpSocket(HttpServer *server) : HttpSocket(), server_(server) {
  stateChanged([this](AbstractSocket::State _state) {
    if (_state != Unconnected) return;
    if (response) response->socket_ = nullptr;
    if (server_) server_->sockets.erase(std::find(server_->sockets.begin(), server_->sockets.end(), this));
    lsInfoCyan() << "unconnected" << peerAddress() << peerPort() << "sockets:" << server_->sockets.size();
    destroy();
  });
  received([this](const DataArray &request) {
    if (server_) server_->received(this, request.view());
    trace() << LogStream::Color::Magenta << header();
    trace() << LogStream::Color::Cyan << content();
  });
  progress([this](int progress) {
    if (server_) server_->fileUploadProgress(this, progress);
  });
  trace();
}

HttpServer::TcpSocket::~TcpSocket() {
  if (ws_) delete ws_;
  trace();
}

void HttpServer::TcpSocket::readEvent() {
  if (ws_) {
    DataArray _da = read();
    //DataArray _in;
    //_in.resize(_da.size());
    //int _s;
    //int r = ws_->getFrame(_da.data(), _da.size(), _in.data(), _in.size(), &_s);
    lsError() << "websocket protocol read not implemented";
    disconnect();
    return;
  }
  HttpSocket::readEvent();
}

HttpServer::HttpRule::~HttpRule() {
  if (exec) delete exec;
}

HttpServer::HttpRule::HttpRule(HttpRule &&r) : method(r.method) {
  exec = r.exec;
  r.exec = nullptr;
}

Instance<HttpServer> HttpServer::instance_ {"HttpServer"};

HttpServer::HttpServer(const std::string &_httpPath) {
  private_ = new Private();
  private_->httpPath = _httpPath;
  addRule("/<arg>", Request::Method::Get, [this](const Request &request) {
    if (private_->httpPath.empty()) {
      lsError("application home not set");
      request.response()->setStatusCode(Response::StatusCode::BadRequest);
      request.response()->send();
      return;
    }
    std::string path = private_->httpPath + ((request.path() == "/") ? "/index.html" : request.path());
    trace("request file: " + path);
    if (std::filesystem::exists(path)) {
      trace(("return: " + path).c_str());
      request.response()->setStatusCode(Response::StatusCode::Ok);
      request.response()->setContent("file://" + path);
      request.response()->send();
      return;
    }
    request.response()->setStatusCode(Response::StatusCode::NotFound);
    request.response()->send();
  });
  lsTrace();
}

HttpServer::~HttpServer() {
  if (instance_.value == this) instance_.value = nullptr;
  if (peek) delete peek;
  clearConnections();
  delete private_;
  lsTrace();
}

void HttpServer::fileUploadProgress(TcpSocket *, int progress) { trace() << progress; }

void HttpServer::disconnectFromHost(TcpSocket *socket) {
  lsTrace();
  AbstractThread::currentThread()->invokeMethod([socket]() { socket->disconnect(); });
}

void HttpServer::sendToWebSockets(const std::string &data) {  //Дичь, для отладки, надо убрать
  std::string _f;

  for (HttpServer::TcpSocket *socket : sockets) {
    if (!socket->ws_) continue;
    if (_f.empty()) {
      _f.resize(data.size() + 1 + 8);
      int size = socket->ws_->makeFrame(WebSocketFrameType::TEXT_FRAME, (unsigned char *)data.data(), data.size(), (unsigned char *)_f.data(), _f.size());
      _f.resize(size);
    }
    socket->write(_f);
  }
}

bool HttpServer::listen(uint16_t port) {
  trace_if(!private_->tlsContext_.empty()) << private_->tlsContext_.infoCertificate();
  bool b = private_->listener.listen("0.0.0.0", port);
  if (b) {
    private_->listenerGuard = private_->listener.incoming([this](int descriptor, const std::string &address, bool *accept) {
      incoming(descriptor, address, accept);
      if (!*accept) {
        TcpSocket *socket = new TcpSocket(this);
        sockets.emplace_back(socket);
        trace() << "(incoming) socket created, total:" << sockets.size() << "tls data" << !private_->tlsContext_.empty();
        if (!private_->tlsContext_.empty()) socket->AbstractSocket::setDescriptor(descriptor);
        else {
          socket->setContext(private_->tlsContext_);
          socket->setDescriptor(descriptor);
        }
        *accept = true;
      }
    });
    lsDebug() << LogStream::Color::Green << *this;
  } else {
    lsError() << "Tcp server listen error, port:" << port;
  }
  return b;
}

void HttpServer::close() {
  if (!private_->listener.port()) {
    lsError() << "not listen";
    return;
  }
  lsDebug() << LogStream::Color::Blue << "Tcp server close, port: " + std::to_string(private_->listener.port());
  private_->listener.close();
  private_->listenerGuard = {};
}

bool HttpServer::execRule(const Request &req) {
  RulesMap::iterator rule;
  std::string path = req.path();
  rule = findRule(path, req.method());
  if (rule == rules.end()) {
    for (size_t i = path.find_last_of('/'); i && i != std::string::npos;) {
      int j = path.find_last_of('/', i - 1);
      if ((rule = findRule(std::string(path).replace(j + 1, i - j - 1, "<arg>"), req.method())) != rules.end()) break;
      if ((rule = findRule((path.substr(0, i) + "/<arg>"), req.method())) != rules.end()) break;
      i = j;
    }
    if (rule == rules.end() && (rule = findRule("/<arg>", req.method())) == rules.end()) {
      lsWarning() << "Rule not found:" << req.path() << "method:" << req.methodName();
      return false;
    }
  }
  (*rule->second->exec)(req);
  return true;
}

uint16_t HttpServer::port() { return private_->listener.port(); }

void HttpServer::received(TcpSocket *socket, const std::string_view &ba) {
  trace() << LogStream::Color::Red << ba;
  Request req(ba);

  if (!req.fail()) {
    req.response_ = new Response();
    req.response_->socket_ = socket;
    socket->response = req.response_;

    if (req.private_->request.versionMajor == 1 && req.private_->request.versionMinor == 0) {
      socket->connectionClose = true;
      req.response_->version = "1.0";
    }
    req.response_->cors_headers_enabled = cors_request_enabled;

    if (!execRule(req)) {
      req.response_->setStatusCode(Response::StatusCode::BadRequest);
      req.response_->send();
      lsWarning() << "Rule not found:" << req.path() << "method:" << req.methodName();
    }
  } else {
    lsTrace();
    socket->disconnect();
  }
}

int HttpServer::makeWebSocketFrame(const DataArray &_da, DataArray *_f) {
  _f->resize(_da.size() + 1 + 8);
  int size = WebSocket::makeFrame(WebSocketFrameType::TEXT_FRAME, _da.data(), _da.size(), _f->data(), _f->size());
  _f->resize(size);
  return size;
}

HttpServer::RulesMap::iterator HttpServer::findRule(const std::string &path, const Request::Method method) {
  trace(path + " " + std::to_string(static_cast<int>(method)));
  std::pair<std::map<std::string, std::unique_ptr<HttpRule>>::iterator, std::map<std::string, std::unique_ptr<HttpRule>>::iterator> p = rules.equal_range(path);
  if ((p.first == rules.end() || p.first->first != path) && (p.second == rules.end() || p.second->first != path)) { return rules.end(); }
  for (std::map<std::string, std::unique_ptr<HttpRule>>::iterator &it = p.first; it != p.second; ++it) {
    if (it->second->method != method) continue;
    return it;
  }
  trace() << "Method handler not found:" << path << (int)method;
  return rules.end();
}

void HttpServer::setEnableCorsRequests(bool state) { cors_request_enabled = state; }

bool HttpServer::hasTls() { return !private_->tlsContext_.empty(); }

void HttpServer::setTlsContext(const TlsContext &ctx) { private_->tlsContext_ = ctx; }

std::string HttpServer::Response::header() const {
  std::string ba = "HTTP/" + version + ' ' + std::to_string(static_cast<int>(statusCode_)) + "\r\n";
  if (!mimeType_.empty()) ba += "Content-Type: " + mimeType_ + "\r\n";
  else {
    if (!contentLength) ba += "Content-Type: application/x-empty\r\n";
    else { ba += "Content-Type: text/plain\r\n"; }
  }
  ba += "Content-Length: " + std::to_string(contentLength) + "\r\n";

  for (auto &item : additionalHeaders) { ba += item + "\r\n"; }

  if (cors_headers_enabled == true) {
    ba += "Access-Control-Allow-Origin: *\r\n";
    ba += "Access-Control-Allow-Methods: GET, POST, PATCH, PUT, DELETE, OPTIONS\r\n";
    ba += "Access-Control-Allow-Headers: *\r\n";
  }

  ba += "\r\n";
  return ba;
}

void HttpServer::Response::destroy() {
  if (socket_) socket_->response = nullptr;
  AbstractThread::currentThread()->invokeMethod([_p = this]() { delete _p; });
}

bool HttpServer::Response::send() {
  if (!socket_) {
    lsWarning() << "unknown socket";
    destroy();
    return false;
  }
  if (content_.view().find("file://") == 0) {
    std::string fn(content_.begin() + 7, content_.end());
    if (std::filesystem::exists(fn)) {
      if (mimeType_.empty()) {
        std::string ext = std::filesystem::path(fn).extension().string();
        if (!ext.empty()) ext = std::filesystem::path(fn).extension().string().substr(1);
        std::map<std::string, std::string>::iterator it = Private::mimeTypes.find(ext);
        mimeType_ = (it != Private::mimeTypes.end()) ? it->second.c_str() : "application/octet-stream";
      }
      contentLength = std::filesystem::file_size(fn);
      trace() << LogStream::Color::Green << header();
      socket_->write(header());
      socket_->sendFile(fn);
      destroy();
      return true;
    }
    statusCode_ = Response::StatusCode::BadRequest;
    trace() << LogStream::Color::Green << header();
    socket_->write(header());
    destroy();
    return true;
  }
  trace() << LogStream::Color::Green << header();
  socket_->write(header());
  if (!content_.empty()) socket_->write(content_);
  destroy();
  return true;
}

void HttpServer::Response::setContent(const DataArray &ba) {
  content_ = ba;
  contentLength = ba.size();
}

void HttpServer::Response::setContent(const std::vector<uint8_t> &v) {
  content_ = DataArray(v.begin(), v.end());
  contentLength = content_.size();
}

HttpServer::Request::Request(const std::string_view &str) {
  private_ = new Private(str);

  if (private_->res != httpparser::HttpRequestParser::ParsingCompleted) {
    method_ = Method::Unknown;
    lsWarning() << "Parsing failed\n" << str;
    return;
  }

  if (private_->request.method == "GET") method_ = Method::Get;
  else if (private_->request.method == "PUT")
    method_ = Method::Put;
  else if (private_->request.method == "DELETE")
    method_ = Method::Delete;
  else if (private_->request.method == "POST")
    method_ = Method::Post;
  else if (private_->request.method == "HEAD")
    method_ = Method::Head;
  else if (private_->request.method == "OPTIONS")
    method_ = Method::Options;
  else if (private_->request.method == "PATCH")
    method_ = Method::Patch;
  else if (private_->request.method == "CONNECT")
    method_ = Method::Connect;
  else if (private_->request.method == "TRACE")
    method_ = Method::Trace;
  else
    method_ = Method::Unknown;
}

HttpServer::Request::~Request() { delete private_; }

std::string HttpServer::Request::methodName() const { return private_->request.method; }

std::string HttpServer::Request::path() const { return private_->uri->path; }

std::string HttpServer::Request::heaaderItemValue(const std::string &name) const {
  for (const httpparser::Request::HeaderItem &item : private_->request.headers) {
    if (name.size() == item.name.size()) {
      for (size_t i = 0; i != name.size(); ++i) {
        if (std::tolower(name[i]) != std::tolower(item.name[i])) goto FAIL;
      }
      return item.value;
    FAIL:
      continue;
    }
  }
  return "";
}

std::string HttpServer::Request::queryItemValue(const std::string &name) const {
  for (std::pair<std::string, std::string> q : private_->uri->query)
    if (q.first == name) return q.second;
  return {};
}

DataArray HttpServer::Request::content() const { return private_->request.content; }

bool HttpServer::Request::switchingProtocols() const {
  response_->statusCode_ = Response::StatusCode::SwitchingProtocols;
  response_->socket_->ws_ = new WebSocket;
  WebSocketFrameType ft = response_->socket_->ws_->parseHandshake(const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(private_->req_.c_str())), private_->req_.size());
  bool b = ft == WebSocketFrameType::OPENING_FRAME;
  if (b) response_->socket_->write(response_->socket_->ws_->answerHandshake());
  else { lsTrace(); }
  response_->destroy();
  return b;
}

bool HttpServer::Request::fail() const { return private_->res != httpparser::HttpRequestParser::ParsingCompleted; }

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const HttpServer &s) {
  std::string str = "Listening: " + ((s.private_->listener.port()) ? s.private_->listener.address() + ':' + std::to_string(s.private_->listener.port()) : "no");
  str += std::string("\nSSL: ") + ((!s.private_->tlsContext_.empty()) ? "enabled" : "disabled");
  str += "\nWeb root: " + ((!s.private_->httpPath.empty()) ? s.private_->httpPath : "none");

  return log << str;
}

}  // namespace AsyncFw
