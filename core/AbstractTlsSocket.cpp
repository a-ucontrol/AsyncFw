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

using namespace AsyncFw;

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
#else
  #define trace() \
    if constexpr (0) LogStream()
#endif

struct AbstractTlsSocket::Private {
  ~Private() {
    if (ssl_) { SSL_free(ssl_); }
    if (bio_) BIO_free(bio_);
  }
  TlsContext ctx_;
  SSL *ssl_ = nullptr;
  BIO *bio_ = nullptr;
  std::vector<uint8_t> raw_input_buffer_;  // Наш промежуточный буфер для недозаписанных данных
  uint8_t encrypt_ = 0;                    // 0 - noencrypt, 1 - server, 2 - client
};

AbstractTlsSocket::AbstractTlsSocket() : AbstractSocket(), private_(*new Private) { trace() << fd_; }

AbstractTlsSocket::~AbstractTlsSocket() {
  delete &private_;
  trace() << fd_;
}

void AbstractTlsSocket::setDescriptor(int fd) {
  if (private_.ctx_.opensslCtx()) private_.encrypt_ = 1;  //server
  else private_.encrypt_ = 0;
  AbstractSocket::setDescriptor(fd);
}

bool AbstractTlsSocket::connect(const std::string &address, uint16_t port) {
  if (private_.ctx_.opensslCtx()) private_.encrypt_ = 2;  //client
  else private_.encrypt_ = 0;
  return AbstractSocket::connect(address, port);
}

void AbstractTlsSocket::disconnect() {
  if (private_.ssl_ && state_ == State::Active) SSL_shutdown(private_.ssl_);
  AbstractSocket::disconnect();
}

void AbstractTlsSocket::close() {
  if (private_.ssl_) {
    SSL_free(private_.ssl_);
    private_.ssl_ = nullptr;
  }
  AbstractSocket::close();
}

void AbstractTlsSocket::setContext(const TlsContext &ctx) const { private_.ctx_ = ctx; }

bool AbstractTlsSocket::contextEmpty() const { return !private_.ctx_.opensslCtx(); }

void AbstractTlsSocket::activateReady() { AbstractSocket::activateEvent(); }

void AbstractTlsSocket::activateEvent() {
  if (state_ != State::Connected) {
    lsError() << "not connected";
    return;
  }
  if (private_.encrypt_ == 0) {
    trace() << fd_ << LogStream::Color::Red << "encryption disabled";
    AbstractSocket::activateEvent();
    return;
  }
  trace() << fd_;
  if (!private_.ssl_) {
    private_.ssl_ = SSL_new(private_.ctx_.opensslCtx());
    if (private_.encrypt_ == 1) SSL_set_ssl_method(private_.ssl_, TLS_server_method());
    else { SSL_set_ssl_method(private_.ssl_, TLS_client_method()); }

    if (private_.ctx_.verifyPeer()) SSL_set_verify(private_.ssl_, SSL_VERIFY_PEER, TlsContext::verify);
    else { SSL_set_verify(private_.ssl_, SSL_VERIFY_NONE, nullptr); }

    BIO *_bio;
    BIO_new_bio_pair(&private_.bio_, 0, &_bio, 0);
    SSL_set_bio(private_.ssl_, _bio, _bio);
    if (!private_.ctx_.verifyName().empty()) {
      lsTrace() << fd_ << "verify name" << LogStream::Color::Green << private_.ctx_.verifyName();
      SSL_set_hostflags(private_.ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
      if (!SSL_set1_host(private_.ssl_, private_.ctx_.verifyName().c_str())) lsError();
    }
  }

  int sys_available = AbstractSocket::read_available_fd();
  if (sys_available > 0) {
    std::vector<uint8_t> handshake_buf(sys_available);
    int r_sys = AbstractSocket::read_fd(handshake_buf.data(), sys_available);
    if (r_sys > 0) { BIO_write(private_.bio_, handshake_buf.data(), r_sys); }
  }
  // Также проталкиваем остатки из raw_input_buffer_, если они там были
  if (!private_.raw_input_buffer_.empty()) {
    int written = BIO_write(private_.bio_, private_.raw_input_buffer_.data(), private_.raw_input_buffer_.size());
    if (written > 0) { private_.raw_input_buffer_.erase(private_.raw_input_buffer_.begin(), private_.raw_input_buffer_.begin() + written); }
  }

  int r = (private_.encrypt_ == 1) ? SSL_accept(private_.ssl_) : SSL_connect(private_.ssl_);
  //SIGPIPE if (private_.encrypt_ == 1) ::close(fd_); void Thread::startedEvent() disabled it

  int pending = BIO_ctrl_pending(private_.bio_);
  if (pending > 0) {
    std::vector<uint8_t> enc_buf(pending);
    int read_bytes = BIO_read(private_.bio_, enc_buf.data(), pending);
    if (read_bytes > 0) {
      // Базовый класс сам запишет недоотправленные байты хендшейка в wda_!
      AbstractSocket::write_fd(enc_buf.data(), read_bytes);
    }
  }

  if (r <= 0) {
    int _e = SSL_get_error(private_.ssl_, r);
    if (_e == SSL_ERROR_WANT_READ) {
      lsTrace() << LogStream::Color::Red << "want read";
      return;
    }
    if (_e == SSL_ERROR_WANT_WRITE) {
      thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn | AbstractThread::PollOut);
      lsDebug() << LogStream::Color::Red << "want write";
      return;
    }
    setError(AbstractSocket::Activate);
    setErrorString("Accept/Connect TLS error");
    close();
    return;
  }
  char name[64];
  X509 *_pc = SSL_get_peer_certificate(private_.ssl_);
  if (_pc) {
    X509_NAME *_n = X509_get_subject_name(_pc);
    X509_free(_pc);
    X509_NAME_get_text_by_NID(_n, NID_commonName, name, sizeof(name));
  } else std::sprintf(name, "no peer cerificate");

  trace() << ((private_.encrypt_ == 1) ? "server" : "client") << "connected" << LogStream::Color::Green << name;
  activateReady();
}

int AbstractTlsSocket::read_available_fd() const {
  if (!private_.encrypt_) { return AbstractSocket::read_available_fd(); }

  bool sys_disconnected = false;

  // 1. Проверяем, сколько байт готово в ОС
  int sys_available = AbstractSocket::read_available_fd();
  if (sys_available == -2) return -2;

  // Если базовый класс вернул -1, значит TCP-соединение разорвано/ошибка
  if (sys_available < 0) {
    sys_disconnected = true;
  } else if (sys_available > 0) {
    size_t old_size = private_.raw_input_buffer_.size();
    private_.raw_input_buffer_.resize(old_size + sys_available);

    int r = const_cast<AbstractTlsSocket *>(this)->AbstractSocket::read_fd(private_.raw_input_buffer_.data() + old_size, sys_available);

    if (r < 0) {
      sys_disconnected = true;
      private_.raw_input_buffer_.resize(old_size);  // Откатываем размер назад
    } else if (r == 0) {
      // Внезапный EOF (FIN пакет от удаленной стороны)
      sys_disconnected = true;
      private_.raw_input_buffer_.resize(old_size);
    } else {
      private_.raw_input_buffer_.resize(old_size + r);
    }
  }

  // 2. Проталкиваем всё, что можем, в OpenSSL BIO
  if (!private_.raw_input_buffer_.empty()) {
    int written = BIO_write(private_.bio_, private_.raw_input_buffer_.data(), private_.raw_input_buffer_.size());
    if (written > 0) { private_.raw_input_buffer_.erase(private_.raw_input_buffer_.begin(), private_.raw_input_buffer_.begin() + written); }
  }

  // 3. Заставляем OpenSSL распарсить то, что вошло в BIO
  uint8_t dummy;
  SSL_peek(private_.ssl_, &dummy, 0);

  // 4. Считаем, сколько РАСШИФРОВАННЫХ байт у нас готово для верхнего уровня
  int decrypted_available = SSL_pending(private_.ssl_);
  // ЛОГИКА ВОЗВРАТА СОСТОЯНИЯ:
  if (decrypted_available > 0) {
    // Если есть расшифрованные данные — всегда отдаем их количество.
    // Приложение должно их вычитать, даже если сокет уже умер.
    return decrypted_available;
  }
  // Если расшифрованных данных нет БОЛЬШЕ НИГДЕ (ни в OpenSSL, ни в сыром буфере)
  if (private_.raw_input_buffer_.empty() && BIO_ctrl_pending(private_.bio_) == 0) {
    if (sys_disconnected) {
      return -1;  // Сигнализируем базовому классу о честном закрытии сокета
    }
  }
  // Данных пока нет, но сокет жив (ждем дальше)
  return 0;
}

int AbstractTlsSocket::read_fd(void *data, int size) {
  if (!private_.encrypt_) return AbstractSocket::read_fd(data, size);
  return SSL_read(private_.ssl_, data, size);
}

int AbstractTlsSocket::write_fd(const void *data, int size) {
  if (!private_.encrypt_) { return AbstractSocket::write_fd(data, size); }

  // 1. Пропускаем данные приложения через OpenSSL.
  // Ему всё равно, свободна ли сеть, у него свои внутренние буферы в паре BIO.
  int r = SSL_write(private_.ssl_, data, size);
  if (r <= 0) {
    int err = SSL_get_error(private_.ssl_, r);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      return 0;  // Нам нужно подождать (неблокирующий режим)
    }
    return r;  // Критическая ошибка TLS
  }

  // 2. Смотрим, сколько зашифрованного выхлопа нагенерировал OpenSSL
  int pending = BIO_ctrl_pending(private_.bio_);
  if (pending > 0) {
    std::vector<uint8_t> enc_buf(pending);
    int read_bytes = BIO_read(private_.bio_, enc_buf.data(), pending);

    if (read_bytes > 0) {
      // 3. Вызываем базовый класс!
      // Если wda_ пуст — он пошлет в сеть и сохранит остаток при EAGAIN.
      // Если wda_ НЕ пуст — он просто допишет новый шифр в хвост к старому шифру!
      AbstractSocket::write_fd(enc_buf.data(), read_bytes);
    }
  }

  // Возвращаем r (размер сырых данных, успешно обработанных SSL_write)
  return r;
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const AbstractTlsSocket &s) { return (log << *static_cast<const AbstractSocket *>(&s)) << (!s.private_.ctx_.empty() ? s.private_.ctx_.commonName() + '/' + (!s.private_.ctx_.verifyName().empty() ? s.private_.ctx_.verifyName() : "\"\"") : "empty"); }
}  // namespace AsyncFw
