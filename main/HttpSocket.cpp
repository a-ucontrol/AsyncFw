/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/Thread.h"
#include "core/LogStream.h"
#include "HttpSocket.h"

#ifdef EXTEND_SOCKET_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

#define SOCKET_WRITE_SIZE (1024 * 16)

using namespace AsyncFw;

struct HttpSocket::Private {
  DataArray received_;
  File file_;
  int progress_;
  std::size_t headerSize_;
  std::size_t contentLength_ = std::string::npos;
  int readTimeout_ = 10000;
  std::size_t maxReadSize_ = 16 * 1024 * 1024;
  int tid_ = -1;
  bool full_ = false;
};

HttpSocket *HttpSocket::create(AsyncFw::Thread *_t) {
  HttpSocket *_s;
  if (_t) _t->invoke([&_s]() { _s = new HttpSocket(); }, true);
  else { _s = new HttpSocket(); }
  return _s;
}

HttpSocket::HttpSocket() : AbstractTlsSocket(), private_(*new Private()) { lsTrace(); }

HttpSocket::~HttpSocket() {
  if (private_.tid_ != -1 && thread_) thread_->removeTimer(private_.tid_);
  delete &private_;
  lsTrace();
}

void HttpSocket::stateEvent() {
  lsTrace() << *this << LogStream::Color::DarkMagenta << "state:" << static_cast<int>(state_);
  if (state_ == Unconnected) {
    if (!private_.full_ && !private_.received_.empty()) {
      private_.full_ = true;
      received(private_.received_);
    }
  } else if (state_ == State::Active) {
    if (private_.tid_ != -1) thread_->modifyTimer(private_.tid_, private_.readTimeout_);
  }
  stateChanged(state_);
}

void HttpSocket::activateEvent() {
  AbstractTlsSocket::activateEvent();
  if (private_.tid_ != -1 || private_.readTimeout_ <= 0) return;
  private_.tid_ = thread_->appendTimerTask(private_.readTimeout_, [this]() {
    thread_->removeTimer(private_.tid_);
    private_.tid_ = -1;
    close();
  });
}

void HttpSocket::readEvent() {
  if (private_.full_) clearReceived();
  trace();

  AsyncFw::DataArray &_da = peek();
  if (private_.received_.size() + _da.size() > private_.maxReadSize_) {
    lsWarning() << "read size limit exceeded:" << private_.received_.size() + _da.size() << ">" << private_.maxReadSize_;
    setErrorString("Read size limit exceeded");
    disconnect();
    return;
  }

  if (private_.tid_ != -1) thread_->modifyTimer(private_.tid_, private_.readTimeout_);

  if (private_.received_.empty()) {
    std::size_t i = _da.view().find("\r\n\r\n");
    if (i == std::string::npos) return;

    private_.headerSize_ = i;

    private_.received_ += read(private_.headerSize_ + 4);  // received_ contains only the header section here

    std::size_t j;
    std::string _lc;
    _lc.resize(private_.received_.size());
    std::transform(private_.received_.begin(), private_.received_.begin() + private_.received_.size(), _lc.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((i = _lc.find("content-length:")) != std::string::npos) {
      if (_lc.find("transfer-encoding:") != std::string::npos) {
        lsWarning("both Content-Length and Transfer-Encoding present");
        disconnect();
        return;
      }
      if ((j = private_.received_.view().find("\r\n", i + 15)) == std::string::npos) {
        lsWarning("malformed content-length header");
        disconnect();
        return;
      }
      std::size_t k = i + 15;
      while (k < j && (private_.received_[k] == ' ' || private_.received_[k] == '\t')) ++k;
      std::size_t value = 0;
      if (k == j) {
        lsWarning("empty content-length value");
        disconnect();
        return;
      }
      for (; k < j; ++k) {
        uint8_t c = private_.received_[k];
        if (c < '0' || c > '9') {
          lsWarning("invalid content-length value");
          disconnect();
          return;
        }
        if (value > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(c - '0')) / 10) {
          lsWarning("content-length overflow");
          disconnect();
          return;
        }
        value = value * 10 + static_cast<std::size_t>(c - '0');
      }
      if (private_.received_.size() + value > private_.maxReadSize_) {
        lsWarning() << "content-length exceeds maxReadSize:" << private_.received_.size() + value << ">" << private_.maxReadSize_;
        setErrorString("Payload too large");
        disconnect();
        return;
      }
      private_.contentLength_ = value;
      if (private_.contentLength_ == 0) {
        private_.full_ = true;
        received(private_.received_);
        return;
      }
    } else if ((i = _lc.find("transfer-encoding:")) != std::string::npos) {
      if ((j = private_.received_.view().find("\r\n", i + 18)) != std::string::npos) {
        std::string str(_lc.begin() + i + 18, _lc.begin() + j);
        if (str.find("chunked") == std::string::npos) {
          lsWarning("error http read");
          disconnect();
          return;
        }
      }
    } else if (_da.empty()) {
      private_.full_ = true;
      received(private_.received_);
      return;
    } else {
      lsDebug() << LogStream::Color::Blue << "error http read" << _da.size() << private_.received_.size();
      disconnect();
      return;
    }
  }

  if (private_.contentLength_ != std::string::npos) {
    private_.received_ += _da;
    _da.clear();
    std::size_t _l = private_.received_.size() - private_.headerSize_ - 4;
    if (_l < private_.contentLength_) return;
    if (_l > private_.contentLength_) {
      lsWarning() << "unexpected data after body:" << _l << "bytes, expected" << private_.contentLength_ << "(" << peerAddress() << ":" << peerPort() << ")";
      private_.contentLength_ = std::string::npos;
      disconnect();
      return;
    }
    private_.contentLength_ = std::string::npos;
    private_.full_ = true;
    received(private_.received_);
    return;
  }

  for (;;) {
    std::size_t _s = _da.view().find("\r\n");
    if (_s == std::string::npos) {
      if (_da.size() > 64) {
        lsWarning("chunk header too long");
        disconnect();
      }
      return;
    }
    std::string str(_da.begin(), _da.begin() + _s);

    std::stringstream _stream;
    _stream << str;
    int _n = 0;
    _stream >> std::hex >> _n;

    if (_stream.fail() || _n < 0) {
      lsWarning("error chunk size");
      disconnect();
      return;
    }

    if (_n == 0) {
      _da.clear();
      private_.full_ = true;
      received(private_.received_);
      return;
    }

    if (_da.size() < _n + _s + 4) return;
    _da.erase(_da.begin(), _da.begin() + _s + 2);
    private_.received_ += read(_n);
    _da.erase(_da.begin(), _da.begin() + 2);

    lsTrace() << "read:" << _n;
  }
}

void HttpSocket::writeEvent() {
  lsTrace();
  if (private_.full_) clearReceived();
  if (pendingWrite() >= SOCKET_WRITE_SIZE) return;
  if (private_.file_.isOpen()) {
    DataArray da = private_.file_.read(SOCKET_WRITE_SIZE / 2);
    if (!da.empty()) write(da);
    da = private_.file_.read(SOCKET_WRITE_SIZE / 2);
    if (!da.empty()) write(da);
    std::streamsize _p = private_.file_.tellg();
    std::size_t _s = private_.file_.size();
    if (_p != -1 && _s > 0 && _s != std::numeric_limits<std::size_t>::max()) {
      int p = _p * 100 / _s;
      if (private_.progress_ != p) progress(private_.progress_ = p);
    }
    if (static_cast<std::size_t>(_p) == _s || private_.file_.fail()) {
      private_.file_.close();
      if (connectionClose || private_.file_.fail()) disconnect();
    }
    return;
  }
  writeContent();
}

bool HttpSocket::connect(const std::string &address, uint16_t port) {
  bool _r = false;
  thread_->invoke([this, address, port, &_r]() { _r = AbstractTlsSocket::connect(address, port); }, true);
  return _r;
}

void HttpSocket::disconnect() {
  thread_->invoke([this]() { AbstractTlsSocket::disconnect(); });
}

void HttpSocket::close() {
  thread_->invoke([this]() {
    if (private_.tid_ != -1) {
      thread_->removeTimer(private_.tid_);
      private_.tid_ = -1;
    }
    AbstractTlsSocket::close();
  }, true);
}

void HttpSocket::destroy() {
  thread_->invoke([this]() { AbstractTlsSocket::destroy(); }, true);
}

DataArrayView HttpSocket::header() { return private_.received_.view(0, private_.headerSize_); }

DataArrayView HttpSocket::content() { return private_.received_.view(private_.headerSize_ + 4); }

void HttpSocket::clearReceived() {
  private_.received_.clear();
  private_.full_ = false;
  private_.contentLength_ = std::string::npos;
  lsTrace();
}

void HttpSocket::sendFile(const std::string &fn) {
  if (private_.file_.isOpen()) {
    lsError() << "file already opened:" << fn;
    return;
  }
  private_.file_.open(fn, std::ios::binary | std::ios::in);
  if (private_.file_.fail()) {
    lsError() << "error open file:" << fn;
    return;
  }
  progress(0);
  private_.progress_ = 0;
  writeEvent();
}

void HttpSocket::setReadTimeout(int timeout) { private_.readTimeout_ = timeout; }

void HttpSocket::setMaxReadSize(std::size_t size) { private_.maxReadSize_ = size; }

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const HttpSocket &s) { return log << static_cast<const AbstractTlsSocket &>(s) << s.private_.full_; }
}  // namespace AsyncFw
