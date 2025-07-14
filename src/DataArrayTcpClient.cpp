#include "core/LogStream.h"

#include "DataArrayTcpClient.h"

using namespace AsyncFw;

DataArrayTcpClient::DataArrayTcpClient(SocketThread *thread, bool) : DataArrayAbstractTcp(thread) { ucTrace(); }

void DataArrayTcpClient::socketStateChanged(const DataArraySocket *socket) {
  bool connected = socket->state() == DataArraySocket::Active;
  if (!connected && socket->state() != DataArraySocket::Unconnected) return;
  thread_->invokeMethod([this, socket, connected]() {
    DataArrayTcpClient::Thread *thread = static_cast<Thread *>(socket->thread());
    if (thread) {
      if (std::find(threads_.begin(), threads_.end(), thread) == threads_.end()) {
        ucWarning() << "socket thread not found";
        return;
      }
      bool b = connected == (socket->state() == DataArraySocket::Active);
      if (b) connectionStateChanged(socket);
    }
  });
  ucDebug((connected) ? "connected" : "disconnected");
}

DataArraySocket *DataArrayTcpClient::createSocket(Thread *thread) {
  bool b = false;
  Thread *clientThread;
  if (thread == nullptr) {
    mutex.lock();
    if (threads_.size() < maxThreads) b = true;
    mutex.unlock();
    if (b) clientThread = createThread();
    else {
      mutex.lock();
      clientThread = static_cast<Thread *>(findMinimalSocketsThread());

      clientThread->mutex.lock();
      bool manyConnections = clientThread->sockets_.size() >= maxSockets;
      clientThread->mutex.unlock();

      if (manyConnections) {
        mutex.unlock();
        logError() << "Many connections";
        return nullptr;
      }
    }
  } else
    clientThread = thread;

  DataArraySocket *socket;
  clientThread->invokeMethod([clientThread, &socket]() { socket = clientThread->createSocket(); }, true);
  if (thread == nullptr && !b) mutex.unlock();
  return socket;
}

void DataArrayTcpClient::removeSocket(DataArraySocket *socket) {
  Thread *thread = static_cast<Thread *>(socket->thread());
  if (thread) thread->invokeMethod([thread, socket]() { thread->removeSocket(socket); }, true);
}

void DataArrayTcpClient::connectToHost(DataArraySocket *socket, const std::string &address, uint16_t port, int timeout) {
  socket->setHost(address, port);
  connectToHost(socket, timeout);
}

void DataArrayTcpClient::connectToHost(const DataArraySocket *socket, int timeout) {
  ucTrace("readTimeout: %d, waitKeepAliveAnswerTimeout: %d, waitForEncryptedTimeout: %d, maxThreads: %d, maxSockets: %d, maxReadBuffers = %d, maxReadSize = %d, maxWriteBuffers = %d, maxWriteSize = %d", readTimeout, waitKeepAliveAnswerTimeout, waitForEncryptedTimeout, maxThreads, maxSockets, maxReadBuffers, maxReadSize, maxWriteBuffers, maxWriteSize);
  Thread *clientThread = static_cast<Thread *>(socket->thread());
  if (!clientThread) {
    logWarning("DataArrayTcpClient: unknown socket");
    return;
  }
  if (std::find(disabledEncrypt_.begin(), disabledEncrypt_.end(), socket->hostAddress()) != disabledEncrypt_.end()) const_cast<DataArraySocket *>(socket)->disableTls();
  clientThread->invokeMethod([&socket, &timeout]() { const_cast<DataArraySocket *>(socket)->connectToHost(timeout); }, true);
  ucTrace();
}

DataArrayTcpClient::Thread::~Thread() { ucTrace(); }

void DataArrayTcpClient::Thread::destroy() {
  for (AbstractSocket *socket : sockets_) removeFilters(static_cast<DataArraySocket *>(socket));
  DataArrayAbstractTcp::Thread::destroy();
  ucTrace();
}

DataArraySocket *DataArrayTcpClient::Thread::createSocket() {
  DataArraySocket *tcpSocket = new DataArraySocket(this);
  TcpExchange *tcpExchange   = new TcpExchange(tcpSocket);
  tcpSocket->installReceiveFilter(tcpExchange);
  socketInit(const_cast<DataArraySocket *>(tcpSocket));
  tcpSocket->setStateChanged([this, tcpSocket](AbstractSocket::State state) {
    if (!tcpSocket->filters().empty()) {
      TcpExchange *tcpExchange = static_cast<TcpExchange *>(tcpSocket->filters().at(0));
      if (tcpExchange->isWait() && (state == AbstractSocket::Closing || state == AbstractSocket::Unconnected)) tcpExchange->wake();
    }
    if (state != AbstractSocket::Connected && state != AbstractSocket::Active && state != AbstractSocket::Unconnected) return;
    client()->socketStateChanged(tcpSocket);
  });
  return tcpSocket;
}

void DataArrayTcpClient::Thread::removeSocket(DataArraySocket *socket) {
  checkCurrentThread();
  removeFilters(socket);
  SocketThread::removeSocket(socket);
  if (sockets_.empty()) destroy();
  pool->thread()->invokeMethod([socket]() {
    socket->destroy();
  });
}

void DataArrayTcpClient::Thread::removeFilters(DataArraySocket *socket) {
  std::vector<DataArraySocket::ReceiveFilter *> filters = socket->filters();
  for (DataArraySocket::ReceiveFilter *filter : filters) {
    socket->removeReceiveFilter(filter);
    static_cast<TcpExchange *>(filter)->wake();
    delete static_cast<TcpExchange *>(filter);
  }
}

DataArrayTcpClient::TcpExchange::TcpExchange(DataArraySocket *socket) : socket(socket) { exchangeId = 0x0000ffff; }

int DataArrayTcpClient::TcpExchange::exchange(const DataArray &wba, DataArray *rba, uint32_t pi, int timeout) {
  if (std::this_thread::get_id() == socket->thread()->id()) {
    logError("DataArrayTcpClient: tried exchange from socket thread");
    return ErrorExchangeFromSocketThread;
  }
  if (socket->transmit(wba, pi)) {
    exchangeByteArray = rba;
    exchangeId        = pi;
    return wait(timeout);
  }
  return ErrorExchangeTransmit;
}

bool DataArrayTcpClient::TcpExchange::received(const DataArray *ba, uint32_t pi) {
  if (exchangeId != pi) return false;
  *exchangeByteArray = *ba;
  exchangeId         = 0x0000ffff;
  wake(static_cast<int>(ba->size()));
  return true;
}

int DataArrayTcpClient::TcpExchange::wait(int timeout) {
  result = -1;
  std::unique_lock<std::mutex> lock(mutex);
  if (timeout) {
    if (waitCondition.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout) result = ErrorExchangeTimeout;
  } else
    waitCondition.wait(lock);
  return result;
}

void DataArrayTcpClient::TcpExchange::wake(int result) {
  this->result = result;
  mutex.lock();
  waitCondition.notify_all();
  mutex.unlock();
}
