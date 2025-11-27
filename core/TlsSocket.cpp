#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "TlsContext.h"
#include "LogStream.h"

#include "TlsSocket.h"

using namespace AsyncFw;

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

struct AbstractTlsSocket::Private {
  ~Private() {
    if (ssl_) { SSL_free(ssl_); }
  }
  TlsContext ctx_;
  SSL *ssl_ = nullptr;
  uint8_t encrypt_ = 0;  // 0 - noencrypt, 1 - server, 2 - client
  int dataIndex;
};

AbstractTlsSocket::AbstractTlsSocket(Thread *_thread) : AbstractSocket(_thread) {
  private_ = new Private;
  trace() << fd_;
}

AbstractTlsSocket::~AbstractTlsSocket() {
  delete private_;
  trace() << fd_;
}

void AbstractTlsSocket::setDescriptor(int _fd) {
  if (private_->ctx_.opensslCtx()) private_->encrypt_ = 1;  //server
  AbstractSocket::setDescriptor(_fd);
}

bool AbstractTlsSocket::connect(const std::string &address, uint16_t port) {
  if (private_->ctx_.opensslCtx()) private_->encrypt_ = 2;  //client
  return AbstractSocket::connect(address, port);
}

void AbstractTlsSocket::disconnect() {
  if (private_->ssl_ && state_ == Active) SSL_shutdown(private_->ssl_);
  AbstractSocket::disconnect();
}

void AbstractTlsSocket::close() {
  if (private_->ssl_) {
    SSL_free(private_->ssl_);
    private_->ssl_ = nullptr;
  }
  AbstractSocket::close();
}

void AbstractTlsSocket::setContext(const TlsContext &ctx) const { private_->ctx_ = ctx; }

struct ie {
  int operator()(int, X509_STORE_CTX *) const { return 1; }
};

void AbstractTlsSocket::acceptEvent() {
  if (state_ != Connected) {
    lsError() << "not connected";
    return;
  }
  if (private_->encrypt_ == 0) {
    trace() << fd_ << LogStream::Color::Red << "encryption disabled";
    AbstractSocket::acceptEvent();
    return;
  }
  trace() << fd_;
  if (!private_->ssl_) {
    private_->ssl_ = SSL_new(private_->ctx_.opensslCtx());
    if (private_->encrypt_ == 1) SSL_set_ssl_method(private_->ssl_, TLS_server_method());
    else { SSL_set_ssl_method(private_->ssl_, TLS_client_method()); }

    if (private_->ctx_.verifyPeer()) SSL_set_verify(private_->ssl_, SSL_VERIFY_PEER, TlsContext::verify);
    else { SSL_set_verify(private_->ssl_, SSL_VERIFY_NONE, nullptr); }

    SSL_set_fd(private_->ssl_, fd_);
    if (!private_->ctx_.verifyName().empty()) {
      lsTrace() << fd_ << "verify name" << LogStream::Color::Green << private_->ctx_.verifyName();
      SSL_set_hostflags(private_->ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      if (!SSL_set1_host(private_->ssl_, private_->ctx_.verifyName().c_str())) lsError();
    }
  }
  int r = (private_->encrypt_ == 1) ? SSL_accept(private_->ssl_) : SSL_connect(private_->ssl_);
  //SIGPIPE if (private_->encrypt_ == 1) ::close(fd_); void Thread::startedEvent() disabled it
  if (r <= 0) {
    r = ERR_peek_error();
    if (!r && SSL_want_read(private_->ssl_)) return;
    setErrorCode(AbstractSocket::Error::Accept);
    setErrorString("Accept TLS error");
    state_ = Error;
    stateEvent();
    close();
    return;
  }
  char name[64];
  X509 *_pc = SSL_get_peer_certificate(private_->ssl_);
  if (_pc) {
    X509_NAME *_n = X509_get_subject_name(_pc);
    X509_free(_pc);
    X509_NAME_get_text_by_NID(_n, NID_commonName, name, sizeof(name));
  } else
    std::sprintf(name, "no peer cerificate");

  trace() << ((private_->encrypt_ == 1) ? "server" : "client") << "connected" << LogStream::Color::Green << name;
  AbstractSocket::acceptEvent();
}

int AbstractTlsSocket::read_available_fd() const {
  int r = AbstractSocket::read_available_fd();
  if (!private_->encrypt_ || r <= 0) return r;
  if (SSL_peek(private_->ssl_, nullptr, 0) < 0) return rs_ = 0;
  return rs_ = SSL_pending(private_->ssl_);
}

int AbstractTlsSocket::read_fd(void *data, int size) {
  if (!private_->encrypt_) return AbstractSocket::read_fd(data, size);
  return SSL_read(private_->ssl_, data, size);
}

int AbstractTlsSocket::write_fd(const void *data, int size) {
  if (!private_->encrypt_) return AbstractSocket::write_fd(data, size);
  return SSL_write(private_->ssl_, data, size);
}
