#include <core/LogStream.h>

#include "HttpSocket.h"

HttpSocket *HttpSocket::create(AsyncFw::Thread *_t) {
  HttpSocket *_s;
  if (_t) _t->invokeMethod([&_s]() { _s = new HttpSocket(); }, true);
  else { _s = new HttpSocket(); }
  return _s;
}

void HttpSocket::stateEvent() {
  lsDebug() << "state event:" << static_cast<int>(state());
  if (state() == Unconnected) {
    if (AbstractSocket::error() > Closed) {
      error(AsyncFw::AbstractSocket::error());
      return;
    }
    receivedHeader(header_);
    if (contentLenght_) receivedContent(content_);
    header_.clear();
    content_.clear();
    return;
  }

  if (state() == State::Active) {
    header_.clear();
    content_.clear();
  }
  stateChanged(state());
}

void HttpSocket::readEvent() {
  lsTrace() << "read event";

  AsyncFw::DataArray &_da = peek();

  if (header_.empty()) {
    std::size_t i = _da.view().find("\r\n\r\n");
    if (i == std::string::npos) return;

    header_ += read(i + 4);

    std::size_t j;
    std::string _lc;
    _lc.resize(header_.size());
    std::transform(header_.begin(), header_.begin() + header_.size(), _lc.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((i = _lc.find("content-length:")) != std::string::npos) {
      if ((j = header_.view().find("\r\n", i + 15)) != std::string::npos) {
        std::string str(header_.begin() + i + 15, header_.begin() + j);
        contentLenght_ = std::stoi(str);
      }
      if (contentLenght_ == 0) {
        receivedHeader(header_);
        header_.clear();
        return;
      }
      if (contentLenght_ == std::string::npos) {
        lsError("error http request");
        disconnect();
        return;
      }
    } else {
      if ((i = _lc.find("transfer-encoding:")) != std::string::npos) {
        if ((j = header_.view().find("\r\n", i + 18)) != std::string::npos) {
          std::string str(header_.begin() + i + 18, header_.begin() + j);
          if (str.find("chunked") == std::string::npos) {
            lsError("error http request");
            disconnect();
            return;
          }
          contentLenght_ = std::string::npos;
        }
      } else
        contentLenght_ = 0;
    }
  }

  if (contentLenght_ != std::string::npos) {
    content_ += _da;
    _da.clear();
    if (contentLenght_ != content_.size()) return;
    receivedContent(content_);
    header_.clear();
    content_.clear();
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

      receivedHeader(header_);
      receivedContent(content_);
      header_.clear();
      content_.clear();
      return;
    }

    if (_da.size() < _n + _s + 4) return;
    _da.erase(_da.begin(), _da.begin() + _s + 2);
    content_ += read(_n);
    _da.erase(_da.begin(), _da.begin() + 2);

    lsDebug() << "readed:" << _n;
  }
}
