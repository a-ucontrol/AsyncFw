#pragma once

#include <functional>
#include <mutex>

namespace AsyncFw {
class AbstractThread;
class FunctionConnectionGuard;

class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  enum ConnectionType : uint8_t { Auto = 0, Queued = 0x01, Direct = 0x02, QueuedSync = 0x03, QueuedOnly = 0x11, DirectOnly = 0x12, QueuedSyncOnly = 0x13 };
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  public:
    enum ConnectionType : uint8_t { Auto2 = AbstractFunctionConnector::Auto, Queued = AbstractFunctionConnector::Queued, Direct = AbstractFunctionConnector::Direct, QueuedSync = AbstractFunctionConnector::QueuedSync, Default = 0x04 };

  protected:
    Connection(AbstractFunctionConnector *, ConnectionType);
    virtual ~Connection() = 0;
    void invoke(const std::function<void(void)> &) const;
    AbstractThread *thread_;

  private:
    AbstractFunctionConnector *connector_;
    FunctionConnectionGuard *guarg_ = nullptr;
    bool sync_ = false;
  };

protected:
  AbstractFunctionConnector(ConnectionType = Queued);
  virtual ~AbstractFunctionConnector() = 0;
  std::vector<Connection *> list_;
  ConnectionType defaultConnectionType;
  AbstractThread *thread;
  std::mutex mutex;
};

template <typename... Args>
class FunctionConnector : public AbstractFunctionConnector {
public:
  using AbstractFunctionConnector::AbstractFunctionConnector;
  template <typename T>
  Connection &operator()(T f, AbstractFunctionConnector::Connection::ConnectionType t = AbstractFunctionConnector::Connection::Default) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  }
  void operator()(Args... args) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const AbstractFunctionConnector::Connection *c : list_) {
      std::function<void(Args...)> f = static_cast<const Connection *>(c)->f;
      if (static_cast<const Connection *>(c)->thread_) static_cast<const Connection *>(c)->invoke([f, args...]() { f(args...); });
      else { f(args...); }
    }
  }

private:
  class Connection : public AbstractFunctionConnector::Connection {
    friend class FunctionConnector<Args...>;

  public:
    Connection(const std::function<void(Args...)> &f, AbstractFunctionConnector *c, ConnectionType t) : AbstractFunctionConnector::Connection(c, t), f(f) {}
    std::function<void(Args...)> f;
  };
};

template <typename F>
class FunctionConnectorProtected {
public:
  template <typename... Args>
  class Connector : private FunctionConnector<Args...> {
    friend F;

  public:
    using FunctionConnector<Args...>::FunctionConnector;
    template <typename T>
    AbstractFunctionConnector::Connection &operator()(T f, AbstractFunctionConnector::Connection::ConnectionType t = AbstractFunctionConnector::Connection::Default) {
      return FunctionConnector<Args...>::operator()(f, t);
    }

  protected:
    void operator()(Args... args) { FunctionConnector<Args...>::operator()(args...); }
  };
};

class FunctionConnectionGuard {
  friend AbstractFunctionConnector::Connection;

public:
  FunctionConnectionGuard();
  FunctionConnectionGuard(FunctionConnectionGuard &&);
  FunctionConnectionGuard(AbstractFunctionConnector::Connection &);
  ~FunctionConnectionGuard();
  void operator=(AbstractFunctionConnector::Connection &);
  void operator=(FunctionConnectionGuard &&);

private:
  AbstractFunctionConnector::Connection *c_;
};

class FunctionConnectionGuardList : public std::vector<FunctionConnectionGuard> {
public:
  void operator+=(FunctionConnectionGuard &&);
};
}  // namespace AsyncFw
