#pragma once

#include <functional>
#include <mutex>

namespace AsyncFw {
class AbstractThread;
class FunctionConnectionGuard;

class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  enum ConnectionType : uint8_t { Queued = 0x01, Direct = 0x02, QueuedSync = 0x03, QueuedOnly = 0x11, DirectOnly = 0x12, QueuedSyncOnly = 0x13 };
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  public:
    enum ConnectionType : uint8_t { Queued = AbstractFunctionConnector::Queued, Direct = AbstractFunctionConnector::Direct, QueuedSync = AbstractFunctionConnector::QueuedSync };

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

  AbstractFunctionConnector(ConnectionType = Queued);

protected:
  virtual ~AbstractFunctionConnector() = 0;
  std::vector<Connection *> list_;
  ConnectionType defaultConnectionType;
  std::mutex mutex;
};

template <typename... Args>
class FunctionConnector : public AbstractFunctionConnector {
public:
  using AbstractFunctionConnector::AbstractFunctionConnector;
  template <typename T>
  Connection &operator()(T f, AbstractFunctionConnector::Connection::ConnectionType t) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  }
  template <typename T>
  Connection &operator()(T f) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, static_cast<AbstractFunctionConnector::Connection::ConnectionType>(defaultConnectionType));
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
    AbstractFunctionConnector::Connection &operator()(T f, AbstractFunctionConnector::Connection::ConnectionType t) {
      return FunctionConnector<Args...>::operator()(f, t);
    }
    template <typename T>
    AbstractFunctionConnector::Connection &operator()(T f) {
      return FunctionConnector<Args...>::operator()(f, static_cast<AbstractFunctionConnector::Connection::ConnectionType>(AbstractFunctionConnector::defaultConnectionType));
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
