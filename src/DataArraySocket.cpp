#include "core/Thread.h"
#include "core/LogStream.h"

#include "DataArraySocket.h"

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

#undef uC_THREAD
#define uC_THREAD this->thread()
using namespace AsyncFw;
DataArraySocket::ReceiveFilter::~ReceiveFilter() { ucTrace(); }

DataArraySocket::DataArraySocket(SocketThread *_thread) : AbstractTlsSocket(_thread) {
  sslConnection                      = 0;
  receiveByteArray                   = nullptr;
  waitTimerType                      = 0;
  waitForConnectTimeoutInterval      = 0;
  reconnectTimeoutInterval           = 0;
  readTimeoutInterval                = 0;
  waitKeepAliveAnswerTimeoutInterval = 0;
  waitForEncryptedTimeoutInterval    = 10000;
  timerId                            = 0;
  port                               = 0;
  hostPort_v                         = 0;
  readSize                           = 0;
  readId                             = 0;
  setReadBuffers(16, 1024 * 1024);
  setWriteBuffers(16, 1024 * 1024);
  trace();
}

DataArraySocket::~DataArraySocket() {
  while (!receiveList.empty()) clearBuffer_(receiveList.back());
  trace();
}

void DataArraySocket::destroy() {
  checkCurrentThread();
  if (receiveList.empty()) {
    AbstractTlsSocket::destroy();
    return;
  }
  thread_->removeSocket(this);
}

void DataArraySocket::stateEvent() {
  if (state_ == Connected) {
    if (waitTimerType & 0x04) {
      waitTimerType &= ~0x04;
      if (sslConnection) {
        startTimer(waitForEncryptedTimeoutInterval);
        ucDebug("client wait for encrypted");
        return;
      }
    }
    return;
  }

  if (wait_holder_) wait_holder_->complete();

  if (state_ == AbstractSocket::Active) {
    if (sslConnection) sslConnection = 4;
    waitTimerType &= ~0x08;
    if (readTimeoutInterval > 0) startTimer(readTimeoutInterval);
    else if (reconnectTimeoutInterval > 0) { removeTimer(); }
  } else if (state_ == AbstractSocket::Unconnected) {
    disconnected();
    if (!(waitTimerType & 0x08)) {
      if (reconnectTimeoutInterval > 0) startTimer(reconnectTimeoutInterval);
      else if (readTimeoutInterval > 0) { removeTimer(); }
    }
    readSize                              = 0;
    std::vector<DataArray *>::iterator it = std::find(receiveList.begin(), receiveList.end(), receiveByteArray);
    if (receiveByteArray && it != receiveList.end()) {
      receiveList.erase(it);
      receiveByteArray = nullptr;
      logWarning("DataArraySocket: disconnected while receive");
    }
    ucWarning("socket unconnected (" + peerString() + ')');
  } else if (state_ == AbstractSocket::Connecting) {
    if (!receiveList.empty()) logWarning("DataArraySocket: receive buffer not empty during connect");
  }

  stateChanged(state_);
}

void DataArraySocket::timerEvent() {
  removeTimer();
  if (state_ == AbstractSocket::Active) {
    if (sslConnection != 3 && waitTimerType == 0x80 && readTimeoutInterval > 0) {
      transmitKeepAlive(true);
    } else {
      std::string e;
      if (sslConnection == 3) e = "Error wait encryption";
      else
        e = (waitTimerType & 0x02) ? "Connection lost" : "Read timeout";
      e += " (" + peerString() + ')';
      setErrorString(e);
      sendMessage(e, LogStream::Error);
      waitTimerType |= 0x01;
      disconnectFromHost();
    }
  } else if (state_ == AbstractSocket::Unconnected) {
    if (reconnectTimeoutInterval > 0) connectToHost();
  } else {
    std::string e;
    if (state_ == Connecting) {
      e = "Timeout while connecting";
    } else if (state_ == Connected) {
      e = "Timeout while wait encryption";
    } else {
      e = "Unknown timeout";
      logError("Unknown timeout (" + peerString() + ')');
    }

    if (wait_holder_) wait_holder_->complete();

    waitTimerType |= 0x01;
    disconnectFromHost();
    e += " (" + peerString() + ')';
    setErrorString(e);
    sendMessage(e, LogStream::Error);
  }
}

void DataArraySocket::installReceiveFilter(ReceiveFilter *rf) {
  std::lock_guard<std::mutex> lock(mutex);
  receiveFilters.emplace_back(rf);
  ucTrace("filters: " + std::to_string(receiveFilters.size()));
}

void DataArraySocket::removeReceiveFilter(ReceiveFilter *rf) {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<ReceiveFilter *>::iterator it = std::find(receiveFilters.begin(), receiveFilters.end(), rf);
  if (it != receiveFilters.end()) receiveFilters.erase(it);
  ucTrace("filters: " + std::to_string(receiveFilters.size()));
}

void DataArraySocket::transmitKeepAlive(bool request) {
  if (state_ != AbstractSocket::Active) {
    logWarning("DataArraySocket: tried transmit keep alive to inactive socket");
    return;
  }
  uint64_t ka = 0xffffffff00000000;
  write(reinterpret_cast<const uint8_t *>(&ka), sizeof(ka));
  if (request) {
    waitTimerType |= 0x02;
    startTimer(waitKeepAliveAnswerTimeoutInterval);
  }
  trace("transmit keep alive " + std::string(request ? "request" : "answer") + " (" + peerString() + ')');
}

std::string DataArraySocket::peerString() const { return AbstractSocket::peerAddress() + ':' + std::to_string(AbstractSocket::peerPort()); }

void DataArraySocket::readEvent() {
  if (state_ != AbstractSocket::Active) {
    logError("DataArraySocket: tried read from inactive socket");
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
          trace("receive keep alive (" + peerString() + ')');
          if (!(waitTimerType & 0x02)) transmitKeepAlive(false);
          else
            waitTimerType &= ~0x02;
          continue;
        }
        warning_if(readId != 0xffffffff) << LogStream::Color::Red << "read array empty (" + peerString() + ')';
      }
      bool e = false;
      if (readSize > static_cast<uint32_t>(maxReadSize)) {
        sendMessage("Big received size: " + std::to_string(readSize) + "  (" + peerString() + ')', LogStream::Error);
        e = true;
      } else if (static_cast<int>(receiveList.size()) >= maxReadBuffers) {
        sendMessage("Many receive buffers (" + peerString() + ')', LogStream::Error);
        e = true;
      } else {
        uint32_t size = readSize;
        for (const DataArray *ba : receiveList) {
          size += static_cast<uint32_t>(ba->size());
          if (size > static_cast<uint32_t>(maxReadSize)) {
            sendMessage("Receive overflow (" + peerString() + ')', LogStream::Error);
            e = true;
            break;
          }
        }
      }
      if (e) {
        readSize = 0;
        disconnectFromHost();
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
      if (receiveData(receiveByteArray, &readId)) {
        mutex.lock();
        if (!receiveFilters.empty()) {
          for (ReceiveFilter *rf : receiveFilters)
            if (rf->received(receiveByteArray, readId)) {
              mutex.unlock();
              clearBuffer(receiveByteArray);
              goto L1;
            }
        }
        mutex.unlock();
        received(receiveByteArray, readId);
      L1:
        receiveByteArray = nullptr;
      } else {
        disconnectFromHost();
        return;
      }
    }
  }
  if (readTimeoutInterval > 0) startTimer(readTimeoutInterval);
}

void DataArraySocket::errorEvent() { sendMessage(errorString(), LogStream::Error); }

void DataArraySocket::disconnectFromHost() {
  if (state_ == AbstractSocket::Unconnected) {
    logWarning("tried disconnect unconnected socket");
    return;
  }
  if (waitTimerType & 0x40) return;
  waitTimerType |= 0x40;
  if (!(waitTimerType & 0x01)) removeTimer();
  else { waitTimerType &= ~0x01; }
  disconnect();
}

void DataArraySocket::writeSocket() {
  for (;;) {
    if (state_ != AbstractSocket::Active) {
      mutex.lock();
      transmitList = {};
      mutex.unlock();
      logWarning("tried write to unconnected socket");
      return;
    }
    mutex.lock();
    if (transmitList.empty()) {
      mutex.unlock();
      break;
    }
    write(transmitList.front());
    transmitList.pop_front();
    mutex.unlock();
  }
  if (pendingWrite() > maxWriteSize) {
    sendMessage("Write buffer overflow (" + peerString() + ')', LogStream::Error);
    disconnectFromHost();
  }
}

bool DataArraySocket::transmit(const DataArray &ba, uint32_t pi, bool wait) const {
  if (!thread()) {
    ucError("DataArraySocket: thread is nullptr") << static_cast<int>(state_);
    return false;
  }
  if (std::this_thread::get_id() == thread()->id() && wait) {
    logError("DataArraySocket: tried transmit with wait from socket thread");
    return false;
  }
  if (state_ != AbstractSocket::Active) {
    logWarning("DataArraySocket: tried transmit to inactive socket");
    return false;
  }
  warning_if(ba.empty()) << "transmit array empty (" + peerString() + ')';
  if (static_cast<int>(ba.size()) > maxWriteSize) {
    const_cast<DataArraySocket *>(this)->sendMessage("Big transmit size: " + std::to_string(ba.size()) + " (" + peerString() + ')', LogStream::Error);
    if (!hostPort_v) const_cast<DataArraySocket *>(this)->disconnectFromHost();
    return false;
  }
  std::unique_lock<std::mutex> lock(mutex);
  int buffers = transmitList.size();
  if (buffers >= maxWriteBuffers) {
    const_cast<DataArraySocket *>(this)->sendMessage("Many transmit buffers (" + peerString() + ')', LogStream::Error);
    if (!hostPort_v) const_cast<DataArraySocket *>(this)->disconnectFromHost();
    return false;
  }
  int size = 0;
  for (const DataArray &t : transmitList) {
    size += t.size() - 8;
    if (size > maxWriteSize) {
      const_cast<DataArraySocket *>(this)->sendMessage("Transmit overflow (" + peerString() + ')', LogStream::Error);
      if (!hostPort_v) const_cast<DataArraySocket *>(this)->disconnectFromHost();
      return false;
    }
  }

  uint64_t _v = pi;
  _v <<= 32;
  _v |= static_cast<uint32_t>(ba.size());
  DataArray _da(((uint8_t *)&_v), ((uint8_t *)&_v) + 8);
  _da += ba;
  transmitList.push_back(_da);
  if (buffers == 0) thread_->invokeMethod([this]() { const_cast<DataArraySocket *>(this)->writeSocket(); }, wait);
  else {
    if (wait) thread_->invokeMethod([]() {}, wait);
  }
  return true;
}

void DataArraySocket::clearBuffer(const DataArray *da) const {
  if (thread_ && thread_->running()) {
    thread_->invokeMethod([this, da]() { clearBuffer_(da); });
    return;
  }
  clearBuffer_(da);
  if (receiveList.empty()) const_cast<DataArraySocket *>(this)->AbstractTlsSocket::destroy();
}

void DataArraySocket::clearBuffer_(const DataArray *da) const {
  for (std::size_t i = 0; i != receiveList.size(); ++i) {
    if (receiveList[i] == da) {
      receiveList.erase(receiveList.begin() + i);
      delete da;
      return;
    }
  }
  logWarning("DataArraySocket: tried clear missing buffer");
}

void DataArraySocket::error(AbstractSocket::Error error) {
  std::string e = errorString() + ' ' + peerString() + ')';
  setErrorString(e);
  LogStream::MessageType t = (error == AbstractSocket::RemoteHostClosed) ? LogStream::Info : LogStream::Error;
  sendMessage(e, t);
}

void DataArraySocket::sendMessage(const std::string &m, uint8_t t) {
  if (t == LogStream::Error) {
    ucError() << m;
    return;
  }
  ucTrace() << LogStream::Color::Red << LogStream::levelName(t) << LogStream::Color::DarkRed << m << fd_;
}

void DataArraySocket::initServerConnection() {
  address = peerAddress();
  port    = peerPort();
  if (sslConnection) {
    sslConnection = 3;
    startTimer(waitForEncryptedTimeoutInterval);
    ucTrace("server wait for encrypted");
  }
}

void DataArraySocket::connectToHost() {
  checkCurrentThread();
  if (hostAddress_v.empty() || !hostPort_v) {
    ucWarning("empty host address or port");
    return;
  }
  if (state_ != AbstractSocket::Unconnected) {
    ucWarning("trying connect while not unconnected state");
    return;
  }
  waitTimerType &= 0x84;

  if (!(waitTimerType & 0x04)) {
    waitTimerType |= 0x04;
    if (waitForConnectTimeoutInterval > 0) startTimer(waitForConnectTimeoutInterval);
    else { removeTimer(); }
  }

  address = hostAddress_v;
  port    = hostPort_v;
  if (sslConnection == 1) {
    sendMessage("SSL configuration error", LogStream::Error);
    return;
  }
  if (sslConnection) {
    sslConnection = 3;
    AbstractTlsSocket::connect(address, port);
  } else
    AbstractSocket::connect(address, port);

  ucTrace();
}

void DataArraySocket::connectToHost(int timeout) {
  if (wait_holder_) {
    logError() << "Connect in process";
    return;
  }

  if (!timeout) {
    connectToHost();
    return;
  }

  thread_->invokeMethod(
      [this, timeout]() {
        waitTimerType |= 0x04;
        startTimer(timeout);
        connectToHost();
      },
      true);

  ExecLoopThread::Holder h;
  wait_holder_ = &h;
  h.wait();
  wait_holder_ = nullptr;

  ucTrace();
}

void DataArraySocket::disableTls() { sslConnection = 0; }

bool DataArraySocket::initTls(const TlsContext &data) {
  if (!data.verify()) {
    sslConnection = 1;
    ucError("certificate verify error");
    return false;
  }
  setContext(&data);
  sslConnection = 2;
  setIgnoreErrors(data.ignoreErrors());
  if (!data.verifyName().empty()) setVerifyName(data.verifyName());
  trace();
  return true;
}
