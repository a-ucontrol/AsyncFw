#include <fstream>
#include <core/LogStream.h>

#include "HttpSocket.h"

#define SOCKET_WRITE_SIZE 8192

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
  lsDebug() << "state event:" << static_cast<int>(state_);
  if (state_ == Unconnected) {
    if (AbstractSocket::error() > Closed) error(AsyncFw::AbstractSocket::error());
    else if (contentLenght_ != std::string::npos) { received(received_); }
  } else if (state_ == State::Active) {
    received_.clear();
    if (tid_ != -1) thread_->modifyTimer(tid_, 5000);
  }
  stateChanged(state_);
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
    headerSize_ = i;
    contentLenght_ = std::string::npos;

    received_ += read(headerSize_ + 4);

    std::size_t j;
    std::string _lc;
    _lc.resize(received_.size());
    std::transform(received_.begin(), received_.begin() + received_.size(), _lc.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((i = _lc.find("content-length:")) != std::string::npos) {
      if ((j = received_.view().find("\r\n", i + 15)) != std::string::npos) {
        std::string str(received_.begin() + i + 15, received_.begin() + j);
        contentLenght_ = std::stoi(str);
      }
      if (contentLenght_ == 0) {
        received(received_);
        return;
      }
      if (contentLenght_ == std::string::npos) {
        lsError("error http read");
        disconnect();
        return;
      }
    } else if ((i = _lc.find("transfer-encoding:")) != std::string::npos) {
      if ((j = received_.view().find("\r\n", i + 18)) != std::string::npos) {
        std::string str(received_.begin() + i + 18, received_.begin() + j);
        if (str.find("chunked") == std::string::npos) {
          lsError("error http read");
          disconnect();
          return;
        }
      }
    } else if (_da.empty()) {
      received(received_);
      return;
    } else {
      lsError("error http read");
      disconnect();
      return;
    }
  }

  if (contentLenght_ != std::string::npos) {
    received_ += _da;
    _da.clear();
    if (contentLenght_ != received_.size() - headerSize_ - 4) return;
    contentLenght_ = std::string::npos;
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

void HttpSocket::writeEvent() {
  if (pendingWrite() >= SOCKET_WRITE_SIZE) return;
  if (file_.fstream().is_open()) {
    DataArray da;
    da.resize(SOCKET_WRITE_SIZE);
    int r = file_.fstream().readsome(reinterpret_cast<char *>(da.data()), da.size());
    if (r > 0) write(da.data(), r);
    int p = file_.fstream().tellg() * 100 / file_.size();
    if (progress_ != p) progress(progress_ = p);
    if (file_.fstream().tellg() == file_.size() || file_.fail()) {
      file_.close();
      if (connectionClose || file_.fail()) disconnect();
    }
    return;
  }
  writeContent();
}

void HttpSocket::clear() { received_.clear(); }

DataArrayView HttpSocket::header() { return received_.view(0, headerSize_); }

DataArrayView HttpSocket::content() { return received_.view(headerSize_ + 4); }

void HttpSocket::sendFile(const std::string &fn) {
  if (file_.fstream().is_open()) {
    lsError("file already opened") << fn;
    return;
  }
  file_.open(fn, std::ios::binary | std::ios::in);
  if (file_.fail()) {
    lsError("error open file: " + fn);
    return;
  }
  progress(0);
  progress_ = 0;
  writeEvent();
}
