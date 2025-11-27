#include "core/LogStream.h"

#include "DataArraySocket.h"

#include "DataArrayAbstractTcp.h"

using namespace AsyncFw;

DataArrayAbstractTcp::DataArrayAbstractTcp(const std::string &name, AbstractThread *thread) : AbstractThreadPool(name, thread) { init(); }

DataArrayAbstractTcp::Thread *DataArrayAbstractTcp::findMinimalSocketsThread() {
  int index = -1;
  for (int i = 0, n = threads_.size(), m = maxSockets; i != n; ++i) {
    static_cast<Thread *>(threads_.at(i))->mutex.lock();
    int size = static_cast<Thread *>(threads_.at(i))->sockets_.size();
    static_cast<Thread *>(threads_.at(i))->mutex.unlock();
    if (size < m) {
      m = size;
      index = i;
      if (m == 0) {
        lsError("empty sockets list in thread");
        break;
      }
    }
  }
  AbstractThread *t = (index >= 0) ? threads_.at(index) : (threads_.empty() ? nullptr : threads_.at(0));
  return static_cast<Thread *>(t);
}

int DataArrayAbstractTcp::transmit(const DataArraySocket *socket, const DataArray &ba, uint32_t pi, bool wait) {
  if (socket->state() != AbstractSocket::State::Active) return ErrorTransmitNotActive;
  if (!socket->thread()) return ErrorTransmitInvoke;
  bool b = const_cast<DataArraySocket *>(socket)->transmit(ba, pi, wait);
  return (b) ? 0 : ErrorTransmit;
}

void DataArrayAbstractTcp::disconnectFromHost(const DataArraySocket *socket) {
  Thread *thread = static_cast<Thread *>(socket->thread());
  if (!thread) return;
  thread->invokeMethod([socket]() {
    const_cast<DataArraySocket *>(socket)->waitTimerType |= 0x08;
    const_cast<DataArraySocket *>(socket)->disconnect();
  });
}

void DataArrayAbstractTcp::setEncryptDisabled(const std::string &address, bool disabled) {
  std::vector<std::string>::iterator it = std::find(disabledEncrypt_.begin(), disabledEncrypt_.end(), address);
  if (!disabled) {
    if (it != disabledEncrypt_.end()) disabledEncrypt_.erase(it);
  } else if (it == disabledEncrypt_.end()) {
    disabledEncrypt_.emplace_back(address);
  }
}

void DataArrayAbstractTcp::initTls(DataArraySocket *socket, const TlsContext &data) { socket->initTls(data); }

int DataArrayAbstractTcp::sockets(std::vector<DataArraySocket *> *list) {
  int count = 0;
  mutex.lock();
  for (AbstractThread *thread : threads_) {
    static_cast<Thread *>(thread)->mutex.lock();
    if (list) reinterpret_cast<std::vector<AbstractSocket *> *>(list)->insert(reinterpret_cast<std::vector<AbstractSocket *> *>(list)->end(), static_cast<Thread *>(thread)->sockets_.begin(), static_cast<Thread *>(thread)->sockets_.end());
    count += static_cast<Thread *>(thread)->sockets_.size();
    static_cast<Thread *>(thread)->mutex.unlock();
  }
  mutex.unlock();
  return count;
}

void DataArrayAbstractTcp::Thread::socketInit(DataArraySocket *socket) {
  DataArrayAbstractTcp *tcp = static_cast<DataArrayAbstractTcp *>(pool);
  socket->setReadTimeout(tcp->readTimeout);
  socket->setWaitKeepAliveAnswerTimeout(tcp->waitKeepAliveAnswerTimeout);
  socket->setWaitForEncryptedTimeout(tcp->waitForEncryptedTimeout);
  socket->setReadBuffers(tcp->maxReadBuffers, tcp->maxReadSize);
  socket->setWriteBuffers(tcp->maxWriteBuffers, tcp->maxWriteSize);
  socket->received([tcp, socket](const DataArray *da, uint32_t pi) {
    tcp->thread_->invokeMethod([tcp, socket, da, pi]() {
      tcp->received(socket, da, pi);
      socket->clearBuffer(da);
    });
  });
  lsTrace() << LogStream::Color::Green << this << LogStream::Color::Magenta << sockets_.size();
}

void DataArrayAbstractTcp::Thread::destroy() {
  lsTrace() << LogStream::Color::Green << this << LogStream::Color::Magenta << sockets_.size();

  static_cast<DataArrayAbstractTcp *>(pool)->removeThread(this);
  AsyncFw::Thread::quit();

  if (std::this_thread::get_id() != this->id()) {
    AsyncFw::Thread::waitFinished();
    delete this;
    return;
  }
  pool->thread()->invokeMethod([this]() {
    AsyncFw::Thread::waitFinished();
    delete this;
  });
}
