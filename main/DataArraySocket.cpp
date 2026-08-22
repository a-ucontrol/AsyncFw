/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include <deque>
#include "core/DataArray.h"
#include "core/TlsContext.h"
#include "core/LogStream.h"
#include "core/Thread.h"
#include "DataArraySocket.h"

#ifdef EXTEND_SOCKET_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

#undef AsyncFw_THREAD
#define AsyncFw_THREAD this->thread()
using namespace AsyncFw;

struct DataArraySocket::Private {
  int sslConnection = 0;
  DataArray *receiveByteArray = nullptr;

  // 0x01 — transient error marker: set before disconnect() on error, cleared inside disconnect() (prevents setting 0x08)
  // 0x02 — waiting for keep-alive response
  // 0x04 — connection attempt timer active (set when initiating connect)
  // 0x08 — explicit user disconnect: persists until stateEvent(Unconnected) to suppress auto-reconnect; also guards against re-entry into disconnect()
  // 0x80 — keep-alive response timeout enabled
  uint8_t flags = 0;

  int maxReadBuffers = 16;
  int maxReadSize = 1024 * 1024;
  int maxWriteBuffers = 16;
  int maxWriteSize = 1024 * 1024;
  int waitForConnectTimeout = 0;
  int reconnectTimeout = 0;
  int readTimeout = 0;
  int waitForEncryptionTimeout = 10000;
  int waitKeepAliveResponseTimeout = 0;
  int timerId = 0;
  uint32_t readSize = 0;
  uint32_t readId = 0;
  uint16_t port = 0;
  std::string address;

  mutable std::vector<DataArray *> receiveList;
  std::deque<DataArray> transmitList;
  int tid = -1;
  AsyncFw::AbstractThread::Waiter waiter;

  void releaseBuffer(const DataArray *) const;
};

void DataArraySocket::Private::releaseBuffer(const DataArray *da) const {
  for (std::size_t i = 0; i != receiveList.size(); ++i) {
    if (receiveList[i] == da) {
      receiveList.erase(receiveList.begin() + i);
      delete da;
      return;
    }
  }
  lsWarning("tried clear missing buffer");
}

DataArraySocket::DataArraySocket() : AbstractTlsSocket(), private_(*new Private()) { trace(); }

DataArraySocket::~DataArraySocket() {
  if (thread_) removeTimer();
  while (!private_.receiveList.empty()) private_.releaseBuffer(private_.receiveList.back());
  delete &private_;
  trace();
}

void DataArraySocket::startTimer(int _ms) {
  if (private_.tid < 0) private_.tid = thread_->appendTimerTask(_ms, [this]() { timerEvent(); });
  else thread_->modifyTimer(private_.tid, _ms);
}

void DataArraySocket::removeTimer() {
  if (private_.tid >= 0) {
    thread_->removeTimer(private_.tid);
    private_.tid = -1;
  }
}

void DataArraySocket::stateEvent() {
  trace() << static_cast<int>(state_);
  if (state_ == State::Connected) {
    if (private_.flags & 0x04) {
      private_.flags &= ~0x04;
      if (private_.sslConnection) {
        startTimer(private_.waitForEncryptionTimeout);
        lsDebug() << "client wait for encrypted" << private_.waitForEncryptionTimeout;
        return;
      }
    }
    return;
  }

  if (private_.waiter.waiting()) private_.waiter.complete();

  if (state_ == AbstractSocket::State::Active) {
    if (private_.sslConnection) private_.sslConnection = 4;
    private_.flags &= ~0x08;
    if (private_.readTimeout > 0) startTimer(private_.readTimeout);
    else if (private_.reconnectTimeout > 0) { removeTimer(); }
  } else if (state_ == AbstractSocket::State::Unconnected) {
    if (!(private_.flags & 0x08)) {
      if (private_.reconnectTimeout > 0) startTimer(private_.reconnectTimeout);
      else if (private_.readTimeout > 0) { removeTimer(); }
    }
    private_.readSize = 0;
    std::vector<DataArray *>::iterator it = std::find(private_.receiveList.begin(), private_.receiveList.end(), private_.receiveByteArray);
    if (private_.receiveByteArray && it != private_.receiveList.end()) {
      private_.receiveList.erase(it);
      private_.receiveByteArray = nullptr;
      lsWarning("disconnected while receive");
    }
    lsTrace() << LogStream::Blue << "socket unconnected (" + peerString() + ')';
  } else if (state_ == AbstractSocket::State::Connecting) {
    if (!private_.receiveList.empty()) lsWarning("receive buffer not empty during connect");
  }

  stateChanged(state_);
}

void DataArraySocket::timerEvent() {
  removeTimer();
  if (state_ == AbstractSocket::State::Active) {
    if (private_.sslConnection != 3 && private_.flags == 0x80 && private_.readTimeout > 0) {
      sendKeepAlive(true);
    } else {
      std::string e;
      if (private_.sslConnection == 3) e = "Error wait encryption";
      else e = (private_.flags & 0x02) ? "Connection lost" : "Read timeout";
      e += " (" + peerString() + ')';
      setErrorString(e);
      private_.flags |= 0x01;
      disconnect();
    }
  } else if (state_ == AbstractSocket::State::Unconnected) {
    if (private_.reconnectTimeout > 0) connectToHost();
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

    if (private_.waiter.waiting()) private_.waiter.complete();

    private_.flags |= 0x01;
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
    private_.flags |= 0x02;
    startTimer(private_.waitKeepAliveResponseTimeout);
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
    bool start = (private_.readSize == 0);
    if (start) {
      if (pendingRead() < static_cast<int>(sizeof(uint64_t))) return;
      read(reinterpret_cast<uint8_t *>(&private_.readSize), sizeof(uint32_t));
      read(reinterpret_cast<uint8_t *>(&private_.readId), sizeof(uint32_t));
      if (private_.readSize == 0) {
        if (private_.readId == 0xffffffff) {
          trace() << "receive keep alive (" + peerString() + ')';
          if (!(private_.flags & 0x02)) sendKeepAlive(false);
          else private_.flags &= ~0x02;
          continue;
        }
        warning_if(private_.readId != 0xffffffff) << LogStream::Color::Red << "read array empty (" + peerString() + ')';
      }
      bool e = false;
      if (private_.readSize > static_cast<uint32_t>(private_.maxReadSize)) {
        setErrorString("Big received size: " + std::to_string(private_.readSize) + "  (" + peerString() + ')');
        e = true;
      } else if (static_cast<int>(private_.receiveList.size()) >= private_.maxReadBuffers) {
        setErrorString("Many receive buffers (" + peerString() + ')');
        e = true;
      } else {
        uint32_t size = private_.readSize;
        for (const DataArray *ba : private_.receiveList) {
          size += static_cast<uint32_t>(ba->size());
          if (size > static_cast<uint32_t>(private_.maxReadSize)) {
            setErrorString("Receive overflow (" + peerString() + ')');
            e = true;
            break;
          }
        }
      }
      if (e) {
        private_.readSize = 0;
        disconnect();
        return;
      }
      private_.receiveByteArray = new DataArray;
      private_.receiveList.emplace_back(private_.receiveByteArray);
    }
    if (private_.readSize > 0) {
      if (!pendingRead()) break;
      DataArray ba = read(private_.readSize - static_cast<uint32_t>(private_.receiveByteArray->size()));
      private_.receiveByteArray->insert(private_.receiveByteArray->end(), ba.begin(), ba.end());
    }
    if (static_cast<uint32_t>(private_.receiveByteArray->size()) == private_.readSize) {
      private_.readSize = 0;
      received(private_.receiveByteArray, private_.readId);
      private_.receiveByteArray = nullptr;
    }
  }
  if (private_.readTimeout > 0) startTimer(private_.readTimeout);
}

void DataArraySocket::disconnect() {
  if (state_ == AbstractSocket::State::Closing || state_ == AbstractSocket::State::Unconnected) {
    lsWarning("tried disconnect closing or unconnected socket");
    return;
  }
  if (private_.flags & 0x08) return;
  if (!(private_.flags & 0x01)) {
    private_.flags |= 0x08;
    removeTimer();
  } else {
    private_.flags &= ~0x01;
  }
  AbstractTlsSocket::disconnect();
}

void DataArraySocket::writeSocket() {
  for (;;) {
    if (state_ != AbstractSocket::State::Active) {
      private_.transmitList = {};
      lsWarning("tried write to unconnected socket");
      return;
    }
    if (private_.transmitList.empty()) { break; }
    write(private_.transmitList.front());
    private_.transmitList.pop_front();
  }
  if (pendingWrite() > private_.maxWriteSize) {
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
  if (static_cast<int>(ba.size()) > private_.maxWriteSize) {
    setErrorString("Big transmit size: " + std::to_string(ba.size()) + " (" + peerString() + ')');
    if (!private_.port) const_cast<DataArraySocket *>(this)->disconnect();
    return false;
  }
  bool _r = false;
  thread_->invoke([this, &_r, &ba, pi, wait]() {
    int buffers = private_.transmitList.size();
    if (buffers >= private_.maxWriteBuffers) {
      setErrorString("Many transmit buffers (" + peerString() + ')');
      if (!private_.port) const_cast<DataArraySocket *>(this)->disconnect();
      return;
    }
    int size = 0;
    for (const DataArray &t : private_.transmitList) {
      size += t.size() - 8;
      if (size > private_.maxWriteSize) {
        setErrorString("Transmit overflow (" + peerString() + ')');
        if (!private_.port) const_cast<DataArraySocket *>(this)->disconnect();
        return;
      }
    }

    uint64_t _v = pi;
    _v <<= 32;
    _v |= static_cast<uint32_t>(ba.size());
    DataArray _da(((uint8_t *)&_v), ((uint8_t *)&_v) + 8);
    _da += ba;
    private_.transmitList.push_back(_da);
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

void DataArraySocket::setConnectTimeout(int timeout) { private_.waitForConnectTimeout = timeout; }

void DataArraySocket::setReconnectTimeout(int timeout) { private_.reconnectTimeout = timeout; }

void DataArraySocket::setReadTimeout(int timeout) { private_.readTimeout = timeout; }

void DataArraySocket::setWaitForEncryptionTimeout(int timeout) { private_.waitForEncryptionTimeout = timeout; }

void DataArraySocket::setWaitKeepAliveResponseTimeout(int timeout) { ((private_.waitKeepAliveResponseTimeout = timeout) > 0) ? private_.flags |= 0x80 : private_.flags &= ~0x80; }

void DataArraySocket::setReadBuffers(int buffers, int size) {
  private_.maxReadBuffers = buffers;
  private_.maxReadSize = size;
}

void DataArraySocket::setWriteBuffers(int buffers, int size) {
  private_.maxWriteBuffers = buffers;
  private_.maxWriteSize = size;
}

void DataArraySocket::releaseBuffer(const DataArray *da) const {
  if (thread_) thread_->invoke([this, da]() { private_.releaseBuffer(da); });
}

void DataArraySocket::initServerConnection() {
  private_.address = peerAddress();
  private_.port = peerPort();
  lsTrace();
  if (!contextEmpty()) {
    private_.sslConnection = 3;
    startTimer(private_.waitForEncryptionTimeout);
    lsDebug() << "server wait for encrypted:" << private_.waitForEncryptionTimeout;
  }
}

void DataArraySocket::setHost(const std::string &address, uint16_t port) const {
  private_.address = address;
  private_.port = port;
}

const std::string DataArraySocket::hostAddress() const { return private_.address; }

uint16_t DataArraySocket::hostPort() const { return private_.port; }

bool DataArraySocket::connectToHost() {
  checkCurrentThread();
  if (private_.address.empty() || !private_.port) {
    lsWarning("empty host address or port");
    return false;
  }
  if (state_ != AbstractSocket::State::Unconnected) {
    lsWarning("trying connect while not unconnected state");
    return false;
  }
  private_.flags &= 0x84;

  if (!(private_.flags & 0x04)) {
    private_.flags |= 0x04;
    if (private_.waitForConnectTimeout > 0) startTimer(private_.waitForConnectTimeout);
    else { removeTimer(); }
  }

  if (!contextEmpty()) private_.sslConnection = 3;
  return AbstractTlsSocket::connect(private_.address, private_.port);
}

bool DataArraySocket::connectToHost(int timeout) {
  if (private_.waiter.waiting()) {
    lsError() << "connect in process";
    return false;
  }

  if (!timeout) {
    connectToHost();
    return false;
  }

  thread_->invoke([this, timeout]() {
    private_.flags |= 0x04;
    startTimer(timeout);
    connectToHost();
  }, true);

  private_.waiter.wait();

  lsTrace();
  return true;
}

bool DataArraySocket::connect(const std::string &address, uint16_t port) {
  setHost(address, port);
  return connectToHost();
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArraySocket &s) { return (log << *static_cast<const AbstractTlsSocket *>(&s)) << '-' << s.private_.readTimeout << s.private_.waitKeepAliveResponseTimeout; }
}  // namespace AsyncFw
