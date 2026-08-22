/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "TlsContext.h"
#include "Thread.h"
#include "LogStream.h"

#include "AbstractTlsSocket.h"

//#define USE_SSL_BIO_PAIR

using namespace AsyncFw;

#ifdef EXTEND_SOCKET_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "extend_trace.hpp"

struct AbstractTlsSocket::Private {
  ~Private() {
    if (ssl) {
      SSL_free(ssl);
#ifdef USE_SSL_BIO_PAIR
      BIO_free(bio);
#endif
    }
  }
  TlsContext ctx;
  SSL *ssl = nullptr;
  uint8_t encrypt = 0;  // 0 - noencrypt, 1 - server, 2 - client
#ifdef USE_SSL_BIO_PAIR
  BIO *bio = nullptr;
  std::vector<uint8_t> input_buffer;
#endif
};

#ifndef USE_SSL_BIO_PAIR
AbstractTlsSocket::AbstractTlsSocket() : AbstractSocket(Application), private_(*new Private) { trace() << fd_; }
#else
AbstractTlsSocket::AbstractTlsSocket() : AbstractSocket(Network), private_(*new Private) { trace() << fd_; }
#endif

AbstractTlsSocket::~AbstractTlsSocket() {
  delete &private_;
  trace() << fd_;
}

void AbstractTlsSocket::setDescriptor(int fd) {
  if (private_.ctx.opensslCtx()) private_.encrypt = 1;  //server
  else private_.encrypt = 0;
  AbstractSocket::setDescriptor(fd);
}

bool AbstractTlsSocket::connect(const std::string &address, uint16_t port) {
  if (private_.ctx.opensslCtx()) private_.encrypt = 2;  //client
  else private_.encrypt = 0;
  return AbstractSocket::connect(address, port);
}

void AbstractTlsSocket::disconnect() {
  if (private_.ssl && state_ == State::Active) SSL_shutdown(private_.ssl);
  AbstractSocket::disconnect();
}

void AbstractTlsSocket::close() {
  if (private_.ssl) {
    SSL_free(private_.ssl);
#ifdef USE_SSL_BIO_PAIR
    BIO_free(private_.bio);
#endif
    private_.ssl = nullptr;
  }
  AbstractSocket::close();
}

void AbstractTlsSocket::setContext(const TlsContext &ctx) const { private_.ctx = ctx; }

bool AbstractTlsSocket::contextEmpty() const { return !private_.ctx.opensslCtx(); }

void AbstractTlsSocket::activateReady() { AbstractSocket::activateEvent(); }

void AbstractTlsSocket::activateEvent() {
  if (state_ != State::Connected) {
    lsError() << "not connected";
    return;
  }
  if (private_.encrypt == 0) {
    trace() << fd_ << LogStream::Color::Red << "encryption disabled";
    AbstractSocket::activateEvent();
    return;
  }
  trace() << fd_;
  if (!private_.ssl) {
    private_.ssl = SSL_new(private_.ctx.opensslCtx());
    if (private_.encrypt == 1) SSL_set_ssl_method(private_.ssl, TLS_server_method());
    else { SSL_set_ssl_method(private_.ssl, TLS_client_method()); }

    if (private_.ctx.verifyPeer()) SSL_set_verify(private_.ssl, SSL_VERIFY_PEER, TlsContext::verify);
    else { SSL_set_verify(private_.ssl, SSL_VERIFY_NONE, nullptr); }
#ifndef USE_SSL_BIO_PAIR
    SSL_set_fd(private_.ssl, fd_);
#else
    BIO *_bio;
    BIO_new_bio_pair(&private_.bio, 0, &_bio, 0);
    SSL_set_bio(private_.ssl, _bio, _bio);
#endif
    SSL_set_read_ahead(private_.ssl, 1);
    if (!private_.ctx.verifyName().empty()) {
      lsTrace() << fd_ << "verify name" << LogStream::Color::Green << private_.ctx.verifyName();
      SSL_set_hostflags(private_.ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      if (!SSL_set1_host(private_.ssl, private_.ctx.verifyName().c_str())) lsError();
    }
  }
#ifdef USE_SSL_BIO_PAIR
  int _size = AbstractSocket::read_available_fd();
  if (_size > 0) {
    int _v = private_.input_buffer.size();
    private_.input_buffer.resize(_v + _size);
    if (AbstractSocket::read_fd(private_.input_buffer.data() + _v, _size) != _size) {
      lsError() << fd_ << "error read";
      setError(AbstractSocket::Activate);
      setErrorString("Accept/Connect TLS error");
      close();
      return;
    }
  }
  if (!private_.input_buffer.empty()) {
    _size = BIO_write(private_.bio, private_.input_buffer.data(), private_.input_buffer.size());
    if (_size > 0) private_.input_buffer.erase(private_.input_buffer.begin(), private_.input_buffer.begin() + _size);
  }
#endif
  int r = (private_.encrypt == 1) ? SSL_accept(private_.ssl) : SSL_connect(private_.ssl);
  //SIGPIPE if (private_.encrypt == 1) ::close(fd_); void Thread::startedEvent() disabled it
#ifdef USE_SSL_BIO_PAIR
  _size = BIO_ctrl_pending(private_.bio);
  if (_size > 0) {
    std::vector<uint8_t> _buf(_size);
    _size = BIO_read(private_.bio, _buf.data(), _size);
    if (_size > 0) AbstractSocket::write_fd(_buf.data(), _size);
  }
#endif
  if (r <= 0) {
    int _e = SSL_get_error(private_.ssl, r);
    if (_e == SSL_ERROR_WANT_READ) {
      lsTrace() << LogStream::Color::Red << "want read";
      return;
    }
    if (_e == SSL_ERROR_WANT_WRITE) {
#ifndef USE_SSL_BIO_PAIR
      thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn | AbstractThread::PollOut);
#endif
      lsDebug() << LogStream::Color::Red << "want write";
      return;
    }
    setError(AbstractSocket::Activate);
    setErrorString("Accept/Connect TLS error");
    close();
    return;
  }
  char name[64];
  X509 *_pc = SSL_get_peer_certificate(private_.ssl);
  if (_pc) {
    X509_NAME *_n = X509_get_subject_name(_pc);
    X509_free(_pc);
    X509_NAME_get_text_by_NID(_n, NID_commonName, name, sizeof(name));
  } else std::sprintf(name, "no peer cerificate");

  trace() << ((private_.encrypt == 1) ? "server" : "client") << "connected" << LogStream::Color::Green << name;
  activateReady();
}

#ifndef USE_SSL_BIO_PAIR
int AbstractTlsSocket::read_available_fd() const {
  if (!private_.encrypt) return AbstractSocket::read_available_fd();
  if (!private_.ssl) {
    lsError() << "(!private_.ssl)";
    return -2;
  }
  int r = SSL_peek(private_.ssl, nullptr, 0);
  if (r < 0) {
    int e = SSL_get_error(private_.ssl, r);
    if (e == SSL_ERROR_WANT_READ) return 0;
    if (e == SSL_ERROR_WANT_WRITE) {
      thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn | AbstractThread::PollOut);
      return 0;
    }
    return -1;
  }
  r = SSL_pending(private_.ssl);
  return r > 0 ? r : -1;
}
#else
int AbstractTlsSocket::read_available_fd() const {
  if (!private_.encrypt) { return AbstractSocket::read_available_fd(); }
  if (!private_.ssl) {
    lsError() << "(!private_.ssl)";
    return -2;
  }

  int r = SSL_peek(private_.ssl, nullptr, 0);
  if (r < 0) goto READ_FD;
  r = SSL_pending(private_.ssl);
  if (r > 0) return r;

READ_FD:
  int _size = AbstractSocket::read_available_fd();
  if (_size < 0) return _size;
  int _v = private_.input_buffer.size();
  private_.input_buffer.resize(_v + _size);
  r = AbstractSocket::read_fd(private_.input_buffer.data() + _v, _size);
  if (r != _size) return -2;

  r = BIO_write(private_.bio, private_.input_buffer.data(), private_.input_buffer.size());
  if (r > 0) private_.input_buffer.erase(private_.input_buffer.begin(), private_.input_buffer.begin() + r);
  else return 0;

  SSL_peek(private_.ssl, nullptr, 0);
  r = SSL_pending(private_.ssl);
  return r > 0 ? r : -1;
}
#endif

int AbstractTlsSocket::read_fd(void *data, int size) const {
  if (!private_.encrypt) return AbstractSocket::read_fd(data, size);
  return SSL_read(private_.ssl, data, size);
}

#ifndef USE_SSL_BIO_PAIR
int AbstractTlsSocket::write_fd(const void *data, int size) {
  if (!private_.encrypt) return AbstractSocket::write_fd(data, size);
  int r = SSL_write(private_.ssl, data, size);
  if (r <= 0) {
    int e = SSL_get_error(private_.ssl, r);
    if (e == SSL_ERROR_WANT_READ) return 0;
    if (e == SSL_ERROR_WANT_WRITE) {
      thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn | AbstractThread::PollOut);
      return 0;
    }
    return -1;
  }
  return r;
}
#else
int AbstractTlsSocket::write_fd(const void *data, int size) {
  if (!private_.encrypt) { return AbstractSocket::write_fd(data, size); }
  int r = SSL_write(private_.ssl, data, size);
  if (r <= 0) {
    int _e = SSL_get_error(private_.ssl, r);
    if (_e == SSL_ERROR_WANT_READ || _e == SSL_ERROR_WANT_WRITE) return 0;
    return -1;
  }
  int _size = BIO_ctrl_pending(private_.bio);
  if (_size > 0) {
    std::vector<uint8_t> _buf(_size);
    _size = BIO_read(private_.bio, _buf.data(), _size);
    if (_size > 0) AbstractSocket::write_fd(_buf.data(), _size);
  }
  return r;
}
#endif

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const AbstractTlsSocket &s) { return (log << *static_cast<const AbstractSocket *>(&s)) << (!s.private_.ctx.empty() ? s.private_.ctx.commonName() + '/' + (!s.private_.ctx.verifyName().empty() ? s.private_.ctx.verifyName() : "\"\"") : "empty"); }
}  // namespace AsyncFw
