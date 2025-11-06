#include "core/LogStream.h"
#include "Timer.h"
#include "Coroutine.h"

#include "DataArrayTcpClient.h"

using namespace AsyncFw;

DataArrayTcpClient::DataArrayTcpClient(SocketThread *thread) : DataArrayAbstractTcp("TcpClient", thread) { ucTrace(); }

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

int DataArrayTcpClient::exchange(const DataArraySocket *socket, const DataArray &wda, const DataArray *rda, uint32_t pi, int timeout) {
  if (AbstractThread::currentThread() != thread_) return ErrorExchangeThread;
  if (socket->state_ != AbstractSocket::Active) return ErrorExchangeNotActive;
  FunctionConnectionGuardList _gl;
  std::coroutine_handle<> h;
  Timer t;
  int ret = 0;
  _gl += socket->received([pi, &rda, &h](const DataArray *_rda, uint32_t _pi) {
    if (!h || _pi != pi) return;
    rda = _rda;
    h.resume();
  });
  _gl += socket->stateChanged([&h, &ret](AbstractSocket::State state) {
    if (!h || state != AbstractSocket::Unconnected) return;
    ret = ErrorExchangeConnectionClose;
    h.resume();
  });
  _gl += t.timeout([&h, &ret]() {
    ret = ErrorExchangeTimeout;
    h.resume();
  });
  auto _ct([&h, &wda, &ret, socket, pi, timeout, &t]() -> CoroutineTask {
    co_await CoroutineAwait([&h, &wda, &ret, socket, pi, timeout, &t](std::coroutine_handle<> _h) {
      h = _h;
      if (!socket->transmit(wda, pi)) {
        ret = ErrorExchangeTransmit;
        _h.resume();
        return;
      }
      t.start(timeout);
    });
    h = nullptr;
  });
  _ct().wait();
  ucDebug() << ret;
  return ret;
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

DataArraySocket *DataArrayTcpClient::Thread::createSocket() {
  DataArraySocket *tcpSocket = new DataArraySocket(this);
  tcpSocket->setConnectTimeout(client()->waitForConnectTimeoutInterval);
  tcpSocket->setReconnectTimeout(client()->reconnectTimeoutInterval);
  socketInit(const_cast<DataArraySocket *>(tcpSocket));
  tcpSocket->stateChanged([this, tcpSocket](AbstractSocket::State state) {
    if (state != AbstractSocket::Connected && state != AbstractSocket::Active && state != AbstractSocket::Unconnected) return;
    client()->socketStateChanged(tcpSocket);
  });
  return tcpSocket;
}

void DataArrayTcpClient::Thread::removeSocket(DataArraySocket *socket) {
  checkCurrentThread();
  socket->removeTimer();
  SocketThread::removeSocket(socket);
  if (sockets_.empty()) {
    std::vector<AbstractThread *>::iterator it = std::find(static_cast<DataArrayTcpClient *>(pool)->threads_.begin(), static_cast<DataArrayTcpClient *>(pool)->threads_.end(), this);
    if (it != static_cast<DataArrayTcpClient *>(pool)->threads_.end()) destroy();
    else { ucError() << "thread not found"; }
  }
  pool->thread()->invokeMethod([socket]() { socket->destroy(); });
}
