#include "core/LogStream.h"

#include "DataArraySocket.h"

#include "DataArrayTcpServer.h"

using namespace AsyncFw;

DataArrayTcpServer::DataArrayTcpServer(const std::string &name, AsyncFw::Thread *thread) : DataArrayAbstractTcp(name, thread) {
  listener = std::make_unique<ListenSocket>(thread);
  listener->setIncomingConnection([this](int descriptor, const std::string &address) { return incomingConnection(descriptor, address); });
  alwaysConnect_.emplace_back("127.0.0.1");
  lsTrace();
}

void DataArrayTcpServer::quit() {
  close();
  DataArrayAbstractTcp::quit();
}

bool DataArrayTcpServer::incomingConnection(int socketDescriptor, const std::string &address) {
  lsTrace("readTimeout: %d, waitKeepAliveAnswerTimeout: %d, waitForEncryptedTimeout: %d, maxThreads: %d, maxSockets: %d, maxReadBuffers = %d, maxReadSize = %d, maxWriteBuffers = %d, maxWriteSize = %d", readTimeout, waitKeepAliveAnswerTimeout, waitForEncryptedTimeout, maxThreads, maxSockets, maxReadBuffers, maxReadSize, maxWriteBuffers, maxWriteSize);
  Thread *serverThread;
  bool manyConnections = false;
  mutex.lock();
  bool b = (threads_.size() < maxThreads);
  mutex.unlock();
  if (b) serverThread = new Thread(name() + " thread", this);
  else {
    mutex.lock();
    serverThread = static_cast<Thread *>(findMinimalSocketsThread());
    serverThread->mutex.lock();
    manyConnections = serverThread->sockets_.size() >= maxSockets;
    if (manyConnections && std::find(alwaysConnect_.begin(), alwaysConnect_.end(), address) == alwaysConnect_.end()) b = true;
    serverThread->mutex.unlock();
    mutex.unlock();
    if (manyConnections) { lsError() << "many connections"; }
    if (b) return false;
  }

  bool encrypt = std::find(disabledEncrypt_.begin(), disabledEncrypt_.end(), address) == disabledEncrypt_.end() && !tlsData.empty();

  serverThread->invokeMethod([serverThread, socketDescriptor, encrypt]() { serverThread->createSocket(socketDescriptor, encrypt); }, true);
  //serverThread->createSocket(socketDescriptor); //!!! ???
  return true;
}

void DataArrayTcpServer::Thread::createSocket(int socketDescriptor, bool encrypt) {
  DataArraySocket *tcpSocket = new DataArraySocket(this);
  std::string address = tcpSocket->peerAddress();
  socketInit(tcpSocket);
  tcpSocket->initServerConnection();

  tcpSocket->stateChanged([tcpSocket, thread = this, server = server()](AbstractSocket::State state) {
    if (state != AbstractSocket::Unconnected) return;
    thread->removeSocket(tcpSocket);
  });

  if (encrypt) {
    server()->initTls(tcpSocket, server()->tlsData);
    tcpSocket->setDescriptor(socketDescriptor);
  } else
    tcpSocket->AbstractSocket::setDescriptor(socketDescriptor);

  lsTrace("new connection: " + tcpSocket->peerAddress() + ":" + std::to_string(tcpSocket->peerPort()));
}

void DataArrayTcpServer::Thread::removeSocket(DataArraySocket *socket) {
  checkCurrentThread();
  AsyncFw::Thread::removeSocket(socket);
  if (sockets_.empty()) destroy();
  pool->thread()->invokeMethod([socket]() { socket->destroy(); });
}
