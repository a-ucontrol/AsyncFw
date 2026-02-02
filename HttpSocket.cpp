#include <core/LogStream.h>

#include "HttpSocket.h"

using namespace AsyncFw;

HttpSocket *HttpSocket::create(AsyncFw::Thread *_t) {
  HttpSocket *_s;
  if (_t) _t->invokeMethod([&_s]() { _s = new HttpSocket(); }, true);
  else { _s = new HttpSocket(); }
  return _s;
}

HttpSocket::HttpSocket() : AbstractTlsSocket() { lsTrace(); }

HttpSocket::~HttpSocket() {
  if (tid_ != -1) thread_->removeTimer(tid_);
  lsTrace();
}

void HttpSocket::stateEvent() {
  lsDebug() << "state event:" << static_cast<int>(state());
  if (state() == Unconnected) {
    if (AbstractSocket::error() > Closed) {
      error(AsyncFw::AbstractSocket::error());
      return;
    }
    if (!contentLenght_) received(received_);
    return;
  }
  if (state() == State::Active) {
    received_.clear();
    if (tid_ != -1) thread_->modifyTimer(tid_, 5000);
  }
  stateChanged(state());
}

void HttpSocket::activateEvent() {
  AbstractTlsSocket::activateEvent();
  if (tid_ != -1) return;
  tid_ = thread_->appendTimerTask((state_ != Active) ? 10000 : 5000, [this]() {
    thread_->removeTimer(tid_);
    tid_ = -1;
    destroy();
  });
}

void HttpSocket::readEvent() {
  lsTrace() << "read event";

  AsyncFw::DataArray &_da = peek();

  if (received_.empty()) {
    if (tid_ != -1) {
      thread_->removeTimer(tid_);
      tid_ = -1;
    }
    std::size_t i = _da.view().find("\r\n\r\n");
    if (i == std::string::npos) return;

    received_ += read(i + 4);

    std::size_t j;
    std::string _lc;
    _lc.resize(received_.size());
    std::transform(received_.begin(), received_.begin() + received_.size(), _lc.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((i = _lc.find("content-length:")) != std::string::npos) {
      if ((j = received_.view().find("\r\n", i + 15)) != std::string::npos) {
        std::string str(received_.begin() + i + 15, received_.begin() + j);
        contentLenght_ = std::stoi(str);
        headerSize_ = j + 2;
      }
      if (contentLenght_ == 0) {
        received(received_);
        return;
      }
      if (contentLenght_ == std::string::npos) {
        lsError("error http request");
        disconnect();
        return;
      }
    } else {
      if ((i = _lc.find("transfer-encoding:")) != std::string::npos) {
        if ((j = received_.view().find("\r\n", i + 18)) != std::string::npos) {
          std::string str(received_.begin() + i + 18, received_.begin() + j);
          if (str.find("chunked") == std::string::npos) {
            lsError("error http request");
            disconnect();
            return;
          }
          contentLenght_ = std::string::npos;
          headerSize_ = j + 2;
        }
      } else
        contentLenght_ = 0;
    }
  }

  if (contentLenght_ != std::string::npos) {
    received_ += _da;
    _da.clear();
    if (contentLenght_ != received_.size() - headerSize_) return;
    contentLenght_ = 0;
    received(received_);
    return;
  }

  for (;;) {
    std::size_t _s = _da.view().find("\r\n");
    if (_s == std::string::npos) return;
    std::string str(_da.begin(), _da.begin() + _s);

    std::stringstream _stream;
    _stream << str;
    int _n;
    _stream >> std::hex >> _n;

    if (_n == 0) {
      _da.clear();
      received(received_);
      return;
    }

    if (_da.size() < _n + _s + 4) return;
    _da.erase(_da.begin(), _da.begin() + _s + 2);
    received_ += read(_n);
    _da.erase(_da.begin(), _da.begin() + 2);

    lsDebug() << "readed:" << _n;
  }
}

void HttpSocket::writeEvent() { writeContent(); }

void HttpSocket::clear() { received_.clear(); }

DataArrayView HttpSocket::content() { return received_.view(headerSize_); }

DataArrayView HttpSocket::header() { return received_.view(0, headerSize_); }
