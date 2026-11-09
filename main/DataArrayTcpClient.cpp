/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/LogStream.h"
#include "DataArraySocket.h"

#include "DataArrayTcpClient.h"

using namespace AsyncFw;

DataArrayTcpClient::DataArrayTcpClient(const std::string &name) : DataArrayAbstractTcp(name) { lsTrace(); }

DataArrayTcpClient::~DataArrayTcpClient() {
  if (threads_.size() > 0) DataArrayAbstractTcp::quit();
  lsTrace();
}

DataArraySocket *DataArrayTcpClient::createSocket(Thread *thread) {
  bool b = false;
  Thread *clientThread;
  if (thread == nullptr) {
    mutex.lock();
    if (threads_.size() < maxThreads) b = true;
    if (b) {
      clientThread = new Thread(name() + " thread", this);
      mutex.unlock();
      clientThread->start();
    } else {
      clientThread = static_cast<Thread *>(findMinimalSocketsThread());
      mutex.unlock();
      if (!clientThread) {
        lsError() << "many connections";
        return nullptr;
      }
    }
  } else clientThread = thread;

  DataArraySocket *socket;
  clientThread->invoke([clientThread, &socket]() { socket = clientThread->createSocket(); }, true);
  return socket;
}

void DataArrayTcpClient::destroySocket(DataArraySocket *socket) {
  Thread *thread = static_cast<Thread *>(socket->thread());
  if (thread) thread->invoke([thread, socket]() { thread->destroySocket(socket); }, true);
}

void DataArrayTcpClient::connectToHost(DataArraySocket *socket, const std::string &address, uint16_t port) {
  AbstractThread *_t = socket->thread();
  if (!_t) {
    lsWarning() << "socket thread is null";
    return;
  }
  _t->invoke([socket, address, port]() { socket->setHost(address, port); }, true);
  connectToHost(socket);
}

void DataArrayTcpClient::connectToHost(const DataArraySocket *socket) {
  AbstractThread *_t = socket->thread();
  if (!_t) {
    lsWarning() << "socket thread is null";
    return;
  }
  if (std::find(disabledEncryptionHosts_.begin(), disabledEncryptionHosts_.end(), socket->hostAddress()) != disabledEncryptionHosts_.end()) const_cast<DataArraySocket *>(socket)->setContext(TlsContext());
  _t->invoke([socket]() { const_cast<DataArraySocket *>(socket)->connectToHost(); });
  lsTrace("readTimeout: {}, waitKeepAliveAnswerTimeout: {}, waitForEncryptionTimeout: {}, maxThreads: {}, maxSockets: {}, maxReadBuffers = {}, maxReadSize = {}, maxWriteBuffers = {}, maxWriteSize = {}", readTimeout, waitKeepAliveAnswerTimeout, waitForEncryptionTimeout, maxThreads, maxSockets, maxReadBuffers, maxReadSize, maxWriteBuffers, maxWriteSize);
}

DataArrayTcpClient::Thread *DataArrayTcpClient::createThread() {
  Thread *_t;
  {  //lock scope
    AbstractThread::LockGuard lock(mutex);
    _t = new Thread(name() + " thread", this);
  }
  _t->start();
  return _t;
}

DataArrayTcpClient::Thread::~Thread() { lsTrace(); }

DataArraySocket *DataArrayTcpClient::Thread::createSocket() {
  DataArraySocket *tcpSocket = new DataArraySocket();
  tcpSocket->setConnectTimeout(client()->waitForConnectTimeout);
  tcpSocket->setReconnectTimeout(client()->reconnectTimeout);
  tcpSocket->setContext(client()->tlsContext);
  initSocket(const_cast<DataArraySocket *>(tcpSocket));
  tcpSocket->stateChanged.connect([this, tcpSocket](AbstractSocket::State state) {
    if (state != AbstractSocket::State::Active && state != AbstractSocket::State::Unconnected) return;
    client()->thread_->invoke([this, tcpSocket]() { client()->connectionStateChanged(tcpSocket); });
  });
  return tcpSocket;
}
