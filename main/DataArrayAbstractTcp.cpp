/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <openssl/crypto.h>
#include <algorithm>
#include "core/LogStream.h"
#include "DataArraySocket.h"
#include "DataArrayAbstractTcp.h"

using namespace AsyncFw;

DataArrayAbstractTcp::DataArrayAbstractTcp(const std::string &name) : AbstractThreadPool(name) { init(); }

DataArrayAbstractTcp::Thread *DataArrayAbstractTcp::findMinimalSocketsThread() {
  int index = -1;
  for (int i = 0, n = threads_.size(), m = maxSockets; i != n; ++i) {
    {  //lock scope
      Thread::LockGuard lock = threads_.at(i)->lockGuard();
      int size = static_cast<Thread *>(threads_.at(i))->sockets_.size();
      if (size < m) {
        m = size;
        index = i;
        if (m == 0) {
          lsError("empty sockets list in thread");
          break;
        }
      }
    }
  }
  AbstractThread *t = (index >= 0) ? threads_.at(index) : nullptr;
  return static_cast<Thread *>(t);
}

int DataArrayAbstractTcp::transmit(const DataArraySocket *socket, const DataArray &ba, uint32_t pi, bool wait) {
  if (socket->state_ != AbstractSocket::State::Active) return ErrorTransmitNotActive;
  if (!socket->thread()) return ErrorTransmitInvoke;
  bool b = const_cast<DataArraySocket *>(socket)->transmit(ba, pi, wait);
  return (b) ? 0 : ErrorTransmit;
}

void DataArrayAbstractTcp::disconnectFromHost(const DataArraySocket *socket) {
  socket->thread()->invoke([socket]() { const_cast<DataArraySocket *>(socket)->disconnect(); });
}

void DataArrayAbstractTcp::setEncryptionDisabled(const std::string &address, bool disable) {
  std::vector<std::string>::iterator it = std::find(disabledEncryptionHosts_.begin(), disabledEncryptionHosts_.end(), address);
  if (!disable) {
    if (it != disabledEncryptionHosts_.end()) disabledEncryptionHosts_.erase(it);
  } else if (it == disabledEncryptionHosts_.end()) {
    disabledEncryptionHosts_.emplace_back(address);
  }
}

void DataArrayAbstractTcp::setTlsContext(DataArraySocket *socket, const TlsContext &context) { socket->setContext(context); }

int DataArrayAbstractTcp::sockets(std::vector<DataArraySocket *> *list) {
  int count = 0;
  mutex.lock();
  for (AbstractThread *thread : threads_) {
    Thread::LockGuard lock = thread->lockGuard();
    if (list) reinterpret_cast<std::vector<AbstractSocket *> *>(list)->insert(reinterpret_cast<std::vector<AbstractSocket *> *>(list)->end(), static_cast<Thread *>(thread)->sockets_.begin(), static_cast<Thread *>(thread)->sockets_.end());
    count += static_cast<Thread *>(thread)->sockets_.size();
  }
  mutex.unlock();
  return count;
}

void DataArrayAbstractTcp::Thread::initSocket(DataArraySocket *socket) {
  DataArrayAbstractTcp *tcp = static_cast<DataArrayAbstractTcp *>(pool);
  socket->setReadTimeout(tcp->readTimeout);
  socket->setWaitKeepAliveResponseTimeout(tcp->waitKeepAliveAnswerTimeout);
  socket->setWaitForEncryptionTimeout(tcp->waitForEncryptionTimeout);
  socket->setReadBuffers(tcp->maxReadBuffers, tcp->maxReadSize);
  socket->setWriteBuffers(tcp->maxWriteBuffers, tcp->maxWriteSize);
  socket->received.connect([tcp, socket](const DataArray *da, uint32_t pi) {
    tcp->thread_->invoke([tcp, socket, da, pi]() {
      tcp->received(socket, da, pi);
      socket->releaseBuffer(da);
    });
  });
  lsTrace() << LogStream::Color::Green << this << LogStream::Color::Magenta << sockets_.size();
}

void DataArrayAbstractTcp::Thread::destroySocket(DataArraySocket *socket) {
  socket->removeTimer();
  socket->close();
  socket->removeFromThread();
  if (sockets_.empty()) {
    OPENSSL_thread_stop();
    pool->thread()->invoke([this]() { destroy(); });
  }
  if (!pool->thread()->invoke([socket]() { socket->destroy(); })) socket->destroy();
}
