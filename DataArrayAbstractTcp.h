#pragma once

#include "core/FunctionConnector.h"
#include "ThreadPool.h"

namespace AsyncFw {
class DataArraySocket;
class DataArray;
class TlsContext;

class DataArrayAbstractTcp : public AbstractThreadPool {
public:
  enum Result {
    ErrorTransmitInvoke = -100,
    ErrorTransmitNotActive = -101,
    ErrorTransmit = -102,
  };
  DataArrayAbstractTcp(const std::string &, AbstractThread * = nullptr);
  void init(int readTimeout = 30000, int waitKeepAliveAnswerTimeout = 0, int waitForEncryptedTimeout = 10000, int maxThreads = 4, int maxSockets = 8, int maxReadBuffers = 16, int maxReadSize = 16 * 1024 * 1024, int maxWriteBuffers = 16, int maxWriteSize = 16 * 1024 * 1024, int socketReadBufferSize = 1024 * 512) {
    this->readTimeout = readTimeout;
    this->waitKeepAliveAnswerTimeout = waitKeepAliveAnswerTimeout;
    this->waitForEncryptedTimeout = waitForEncryptedTimeout;
    this->maxThreads = maxThreads;
    this->maxSockets = maxSockets;
    this->maxReadBuffers = maxReadBuffers;
    this->maxReadSize = maxReadSize;
    this->maxWriteBuffers = maxWriteBuffers;
    this->maxWriteSize = maxWriteSize;
    this->socketReadBufferSize = socketReadBufferSize;
  }
  int transmit(const DataArraySocket *, const DataArray &, uint32_t, bool = false);
  void disconnectFromHost(const DataArraySocket *);
  int sockets(std::vector<DataArraySocket *> * = nullptr);

  FunctionConnectorProtected<DataArrayAbstractTcp>::Connector<const DataArraySocket *, const DataArray *, uint32_t> received {AbstractFunctionConnector::DirectOnly};

  void setEncryptDisabled(const std::vector<std::string> &list) { disabledEncrypt_ = list; }
  void setEncryptDisabled(const std::string &, bool = true);
  void initTls(DataArraySocket *, const TlsContext &);

protected:
  class Thread : public AbstractThreadPool::Thread {
    friend class DataArrayAbstractTcp;

  public:
    Thread(const std::string &name, DataArrayAbstractTcp *_pool) : AbstractThreadPool::Thread(name, _pool, true), pool(_pool) {};

  protected:
    void socketInit(DataArraySocket *);
    void removeSocket(DataArraySocket *);
    void destroy() override;
    AbstractThreadPool *pool;
  };
  Thread *findMinimalSocketsThread();
  int readTimeout;
  int waitKeepAliveAnswerTimeout;
  int waitForEncryptedTimeout;
  std::size_t maxThreads;
  std::size_t maxSockets;
  int socketReadBufferSize;
  int maxReadBuffers;
  int maxReadSize;
  int maxWriteBuffers;
  int maxWriteSize;
  std::vector<std::string> disabledEncrypt_ = {"127.0.0.1"};
};
}  // namespace AsyncFw
