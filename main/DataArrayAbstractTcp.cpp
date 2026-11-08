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

bool DataArrayAbstractTcp::transmit(const DataArraySocket *socket, const DataArray &ba, uint32_t pi) { return const_cast<DataArraySocket *>(socket)->transmit(ba, pi); }

void DataArrayAbstractTcp::disconnectFromHost(const DataArraySocket *socket) {
  socket->thread()->invoke([socket]() { const_cast<DataArraySocket *>(socket)->disconnect(); }, true);
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

int DataArrayAbstractTcp::sockets(Invocable<void(const DataArraySocket *)>::Abstract *fun) {
  int count = 0;
#if defined(__cpp_range_based_for) && __cpp_range_based_for >= 202211L
  for (AsyncFw::Thread *thread : *threads()) {
#else
  auto locked = threads();
  for (AsyncFw::Thread *thread : *locked) {
#endif
    thread->invoke([thread, fun, &count]() {
      for (const AbstractSocket *s : static_cast<Thread *>(thread)->sockets_) {
        count++;
        if (fun) (*fun)(static_cast<const DataArraySocket *>(s));
      }
    }, true);
  }
  if (fun) delete fun;
  return count;
}

#if defined CHECK_EXECUTION_THREAD
  #define checkCurrentThread() \
    if (std::this_thread::get_id() != id()) lsError() << "executed from different thread"
#else
  #define checkCurrentThread()
#endif

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
  lsTrace() << LogStream::Color::Green << '(' + name() + ')' << LogStream::Color::Magenta << sockets_.size();
}

void DataArrayAbstractTcp::Thread::destroySocket(DataArraySocket *socket) {
  checkCurrentThread();
  socket->close();
  socket->removeTimer();
  socket->removeFromThread();
  bool e = sockets_.empty();
  if (e) OPENSSL_thread_stop();
  if (!pool->thread()->invoke([this, socket, e]() {
    socket->destroy();
    if (e) destroy();
  }))
    socket->destroy();
  lsTrace() << LogStream::Color::Green << '(' + name() + ')' << LogStream::Color::Magenta << sockets_.size();
}
