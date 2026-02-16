#include "core/LogStream.h"

#include "DataArraySocket.h"

#include "DataArrayTcpServer.h"

using namespace AsyncFw;

DataArrayTcpServer::DataArrayTcpServer(const std::string &name) : DataArrayAbstractTcp(name) {
  listener = std::make_unique<ListenSocket>();
  listener->setIncomingConnection([this](int descriptor, const std::string &address) { return incomingConnection(descriptor, address); });
  alwaysConnect_.emplace_back("127.0.0.1");
  lsTrace();
}

void DataArrayTcpServer::quit() {
  close();
  DataArrayAbstractTcp::quit();
}

bool DataArrayTcpServer::listen(const std::string &address, uint16_t port) { return listener->listen(address, port); }

void DataArrayTcpServer::close() { listener->close(); }

void DataArrayTcpServer::setAlwaysConnect(const std::vector<std::string> &list) { alwaysConnect_ = list; }

bool DataArrayTcpServer::listening() { return listener->port() != 0; }

bool DataArrayTcpServer::incomingConnection(int socketDescriptor, const std::string &address) {
  lsTrace("readTimeout: {}, waitKeepAliveAnswerTimeout: {}, waitForEncryptedTimeout: {}, maxThreads: {}, maxSockets: {}, maxReadBuffers = {}, maxReadSize = {}, maxWriteBuffers = {}, maxWriteSize = {}", readTimeout, waitKeepAliveAnswerTimeout, waitForEncryptedTimeout, maxThreads, maxSockets, maxReadBuffers, maxReadSize, maxWriteBuffers, maxWriteSize);
  Thread *serverThread;
  bool manyConnections = false;
  mutex.lock();
  bool b = (threads_.size() < maxThreads);
  mutex.unlock();
  if (b) {
    serverThread = new Thread(name() + " thread", this);
    serverThread->start();
  } else {
    mutex.lock();
    serverThread = static_cast<Thread *>(findMinimalSocketsThread());
    if (!serverThread && std::find(alwaysConnect_.begin(), alwaysConnect_.end(), address) == alwaysConnect_.end()) b = true;
    mutex.unlock();
    if (!serverThread) {
      lsError() << "many connections";
      serverThread = static_cast<Thread *>(threads_.at(0));
    }
    if (b) return false;
  }

  bool encrypt = std::find(disabledEncrypt_.begin(), disabledEncrypt_.end(), address) == disabledEncrypt_.end() && !tlsData.empty();

  serverThread->invokeMethod([serverThread, socketDescriptor, encrypt]() { serverThread->createSocket(socketDescriptor, encrypt); }, true);
  //serverThread->createSocket(socketDescriptor); //!!! ???
  return true;
}

void DataArrayTcpServer::Thread::createSocket(int socketDescriptor, bool encrypt) {
  checkCurrentThread();
  DataArraySocket *tcpSocket = new DataArraySocket();
  std::string address = tcpSocket->peerAddress();
  socketInit(tcpSocket);
  tcpSocket->initServerConnection();

  tcpSocket->stateChanged([tcpSocket, thread = this, server = server()](AbstractSocket::State state) {
    if (state != AbstractSocket::State::Unconnected) return;
    thread->removeSocket(tcpSocket);
  });

  if (encrypt) {
    server()->initTls(tcpSocket, server()->tlsData);
    tcpSocket->setDescriptor(socketDescriptor);
  } else
    tcpSocket->AbstractSocket::setDescriptor(socketDescriptor);

  lsTrace("new connection: " + tcpSocket->peerAddress() + ":" + std::to_string(tcpSocket->peerPort()));
}
