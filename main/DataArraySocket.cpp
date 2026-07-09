/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/DataArray.h"
#include "core/TlsContext.h"
#include "core/LogStream.h"
#include "core/Thread.h"
#include "DataArraySocket.h"

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace() \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

#undef AsyncFw_THREAD
#define AsyncFw_THREAD this->thread()
using namespace AsyncFw;

DataArraySocket::DataArraySocket() : AbstractTlsSocket() {
  sslConnection = 0;
  receiveByteArray = nullptr;
  waitTimerType = 0;
  waitForConnectTimeout_ = 0;
  reconnectTimeout_ = 0;
  readTimeout_ = 0;
  waitKeepAliveResponseTimeout_ = 0;
  waitForEncryptionTimeout_ = 10000;
  timerId = 0;
  port = 0;
  hostPort_ = 0;
  readSize = 0;
  readId = 0;
  setReadBuffers(16, 1024 * 1024);
  setWriteBuffers(16, 1024 * 1024);
  trace();
}

DataArraySocket::~DataArraySocket() {
  if (thread_) removeTimer();
  while (!receiveList.empty()) releaseBuffer_(receiveList.back());
  trace();
}

void DataArraySocket::startTimer(int _ms) {
  if (tid_ < 0) tid_ = thread_->appendTimerTask(_ms, [this]() { timerEvent(); });
  else thread_->modifyTimer(tid_, _ms);
}

void DataArraySocket::removeTimer() {
  if (tid_ >= 0) {
    thread_->removeTimer(tid_);
    tid_ = -1;
  }
}

void DataArraySocket::stateEvent() {
  trace() << static_cast<int>(state_);
  if (state_ == State::Connected) {
    if (waitTimerType & 0x04) {
      waitTimerType &= ~0x04;
      if (sslConnection) {
        startTimer(waitForEncryptionTimeout_);
        lsDebug() << "client wait for encrypted" << waitForEncryptionTimeout_;
        return;
      }
    }
    return;
  }

  if (waiter_.waiting()) waiter_.complete();

  if (state_ == AbstractSocket::State::Active) {
    if (sslConnection) sslConnection = 4;
    waitTimerType &= ~0x08;
    if (readTimeout_ > 0) startTimer(readTimeout_);
    else if (reconnectTimeout_ > 0) { removeTimer(); }
  } else if (state_ == AbstractSocket::State::Unconnected) {
    if (!(waitTimerType & 0x08)) {
      if (reconnectTimeout_ > 0) startTimer(reconnectTimeout_);
      else if (readTimeout_ > 0) { removeTimer(); }
    }
    readSize = 0;
    std::vector<DataArray *>::iterator it = std::find(receiveList.begin(), receiveList.end(), receiveByteArray);
    if (receiveByteArray && it != receiveList.end()) {
      receiveList.erase(it);
      receiveByteArray = nullptr;
      lsWarning("disconnected while receive");
    }
    lsTrace() << LogStream::Blue << "socket unconnected (" + peerString() + ')';
  } else if (state_ == AbstractSocket::State::Connecting) {
    if (!receiveList.empty()) lsWarning("receive buffer not empty during connect");
  }

  stateChanged(state_);
}

void DataArraySocket::timerEvent() {
  removeTimer();
  if (state_ == AbstractSocket::State::Active) {
    if (sslConnection != 3 && waitTimerType == 0x80 && readTimeout_ > 0) {
      sendKeepAlive(true);
    } else {
      std::string e;
      if (sslConnection == 3) e = "Error wait encryption";
      else e = (waitTimerType & 0x02) ? "Connection lost" : "Read timeout";
      e += " (" + peerString() + ')';
      setErrorString(e);
      waitTimerType |= 0x01;
      disconnect();
    }
  } else if (state_ == AbstractSocket::State::Unconnected) {
    if (reconnectTimeout_ > 0) connectToHost();
  } else {
    std::string e;
    if (state_ == State::Connecting) {
      e = "Timeout while connecting";
    } else if (state_ == State::Connected) {
      e = "Timeout while wait encryption";
    } else {
      e = "Unknown timeout";
      lsError("unknown timeout (" + peerString() + ')');
    }

    if (waiter_.waiting()) waiter_.complete();

    waitTimerType |= 0x01;
    disconnect();
    e += " (" + peerString() + ')';
    setErrorString(e);
  }
}

void DataArraySocket::sendKeepAlive(bool request) {
  if (state_ != AbstractSocket::State::Active) {
    lsWarning("tried transmit keep alive to inactive socket");
    return;
  }
  uint64_t ka = 0xffffffff00000000;
  write(reinterpret_cast<const uint8_t *>(&ka), sizeof(ka));
  if (request) {
    waitTimerType |= 0x02;
    startTimer(waitKeepAliveResponseTimeout_);
  }
  trace() << "transmit keep alive " + std::string(request ? "request" : "answer") + " (" + peerString() + ')';
}

std::string DataArraySocket::peerString() const { return AbstractSocket::peerAddress() + ':' + std::to_string(AbstractSocket::peerPort()); }

void DataArraySocket::readEvent() {
  if (state_ != AbstractSocket::State::Active) {
    lsError("tried read from inactive socket");
    return;
  }
  for (;;) {
    if (!pendingRead()) break;
    bool start = (readSize == 0);
    if (start) {
      if (pendingRead() < static_cast<int>(sizeof(uint64_t))) return;
      read(reinterpret_cast<uint8_t *>(&readSize), sizeof(uint32_t));
      read(reinterpret_cast<uint8_t *>(&readId), sizeof(uint32_t));
      if (readSize == 0) {
        if (readId == 0xffffffff) {
          trace() << "receive keep alive (" + peerString() + ')';
          if (!(waitTimerType & 0x02)) sendKeepAlive(false);
          else waitTimerType &= ~0x02;
          continue;
        }
        warning_if(readId != 0xffffffff) << LogStream::Color::Red << "read array empty (" + peerString() + ')';
      }
      bool e = false;
      if (readSize > static_cast<uint32_t>(maxReadSize)) {
        setErrorString("Big received size: " + std::to_string(readSize) + "  (" + peerString() + ')');
        e = true;
      } else if (static_cast<int>(receiveList.size()) >= maxReadBuffers) {
        setErrorString("Many receive buffers (" + peerString() + ')');
        e = true;
      } else {
        uint32_t size = readSize;
        for (const DataArray *ba : receiveList) {
          size += static_cast<uint32_t>(ba->size());
          if (size > static_cast<uint32_t>(maxReadSize)) {
            setErrorString("Receive overflow (" + peerString() + ')');
            e = true;
            break;
          }
        }
      }
      if (e) {
        readSize = 0;
        disconnect();
        return;
      }
      receiveByteArray = new DataArray;
      receiveList.emplace_back(receiveByteArray);
    }
    if (readSize > 0) {
      if (!pendingRead()) break;
      DataArray ba = read(readSize - static_cast<uint32_t>(receiveByteArray->size()));
      receiveByteArray->insert(receiveByteArray->end(), ba.begin(), ba.end());
    }
    if (static_cast<uint32_t>(receiveByteArray->size()) == readSize) {
      readSize = 0;
      received(receiveByteArray, readId);
      receiveByteArray = nullptr;
    }
  }
  if (readTimeout_ > 0) startTimer(readTimeout_);
}

void DataArraySocket::disconnect() {
  if (state_ == AbstractSocket::State::Closing || state_ == AbstractSocket::State::Unconnected) {
    lsWarning("tried disconnect closing or unconnected socket");
    return;
  }
  if (waitTimerType & 0x08) return;
  waitTimerType |= 0x08;
  if (!(waitTimerType & 0x01)) removeTimer();
  else { waitTimerType &= ~0x01; }
  AbstractTlsSocket::disconnect();
}

void DataArraySocket::writeSocket() {
  for (;;) {
    if (state_ != AbstractSocket::State::Active) {
      transmitList = {};
      lsWarning("tried write to unconnected socket");
      return;
    }
    if (transmitList.empty()) { break; }
    write(transmitList.front());
    transmitList.pop_front();
  }
  if (pendingWrite() > maxWriteSize) {
    setErrorString("Write buffer overflow (" + peerString() + ')');
    disconnect();
  }
}

bool DataArraySocket::transmit(const DataArray &ba, uint32_t pi, bool wait) const {
  if (!thread()) {
    lsError("thread is nullptr") << static_cast<int>(state_);
    return false;
  }
  if (std::this_thread::get_id() == thread()->id() && wait) {
    lsError("tried transmit with wait from socket thread");
    return false;
  }
  if (state_ != AbstractSocket::State::Active) {
    lsWarning("tried transmit to inactive socket");
    return false;
  }
  warning_if(ba.empty()) << "transmit array empty (" + peerString() + ')';
  if (static_cast<int>(ba.size()) > maxWriteSize) {
    setErrorString("Big transmit size: " + std::to_string(ba.size()) + " (" + peerString() + ')');
    if (!hostPort_) const_cast<DataArraySocket *>(this)->disconnect();
    return false;
  }
  bool _r = false;
  thread_->invoke([this, &_r, &ba, pi, wait]() {
    int buffers = transmitList.size();
    if (buffers >= maxWriteBuffers) {
      setErrorString("Many transmit buffers (" + peerString() + ')');
      if (!hostPort_) const_cast<DataArraySocket *>(this)->disconnect();
      return;
    }
    int size = 0;
    for (const DataArray &t : transmitList) {
      size += t.size() - 8;
      if (size > maxWriteSize) {
        setErrorString("Transmit overflow (" + peerString() + ')');
        if (!hostPort_) const_cast<DataArraySocket *>(this)->disconnect();
        return;
      }
    }

    uint64_t _v = pi;
    _v <<= 32;
    _v |= static_cast<uint32_t>(ba.size());
    DataArray _da(((uint8_t *)&_v), ((uint8_t *)&_v) + 8);
    _da += ba;
    transmitList.push_back(_da);
    if (buffers == 0) thread_->invoke([this]() { const_cast<DataArraySocket *>(this)->writeSocket(); }, wait);
    else {
      if (wait) {
        thread_->requestInterrupt();
        thread_->waitInterrupted();
      }
    }
    _r = true;
  }, true);
  return _r;
}

void DataArraySocket::releaseBuffer(const DataArray *da) const {
  if (thread_) thread_->invoke([this, da]() { releaseBuffer_(da); });
}

void DataArraySocket::releaseBuffer_(const DataArray *da) const {
  for (std::size_t i = 0; i != receiveList.size(); ++i) {
    if (receiveList[i] == da) {
      receiveList.erase(receiveList.begin() + i);
      delete da;
      return;
    }
  }
  lsWarning("tried clear missing buffer");
}

void DataArraySocket::initServerConnection() {
  address = peerAddress();
  port = peerPort();
  lsTrace();
  if (!contextEmpty()) {
    sslConnection = 3;
    startTimer(waitForEncryptionTimeout_);
    lsDebug() << "server wait for encrypted:" << waitForEncryptionTimeout_;
  }
}

bool DataArraySocket::connectToHost() {
  checkCurrentThread();
  if (hostAddress_.empty() || !hostPort_) {
    lsWarning("empty host address or port");
    return false;
  }
  if (state_ != AbstractSocket::State::Unconnected) {
    lsWarning("trying connect while not unconnected state");
    return false;
  }
  waitTimerType &= 0x84;

  if (!(waitTimerType & 0x04)) {
    waitTimerType |= 0x04;
    if (waitForConnectTimeout_ > 0) startTimer(waitForConnectTimeout_);
    else { removeTimer(); }
  }

  address = hostAddress_;
  port = hostPort_;

  lsTrace() << address << port;

  if (!contextEmpty()) sslConnection = 3;
  return AbstractTlsSocket::connect(address, port);
}

bool DataArraySocket::connectToHost(int timeout) {
  if (waiter_.waiting()) {
    lsError() << "connect in process";
    return false;
  }

  if (!timeout) {
    connectToHost();
    return false;
  }

  thread_->invoke([this, timeout]() {
    waitTimerType |= 0x04;
    startTimer(timeout);
    connectToHost();
  }, true);

  waiter_.wait();

  lsTrace();
  return true;
}

bool DataArraySocket::connect(const std::string &address, uint16_t port) {
  setHost(address, port);
  return connectToHost();
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArraySocket &s) { return (log << *static_cast<const AbstractTlsSocket *>(&s)) << '-' << s.readTimeout_ << s.waitKeepAliveResponseTimeout_; }
}  // namespace AsyncFw
