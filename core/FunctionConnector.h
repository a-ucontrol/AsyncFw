#pragma once

#include <functional>
#include <mutex>
#include "Thread.h"

namespace AsyncFw {
class FunctionConnectionGuard;

class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  enum ConnectionType : uint8_t { Auto = 0, AutoSync = 0x01, Queued = 0x02, Direct = 0x04, QueuedSync = 0x08, AutoOnly = 0x10, AutoSyncOnly = 0x11, QueuedOnly = 0x12, DirectOnly = 0x14, QueuedSyncOnly = 0x18 };
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  public:
    enum ConnectionType : uint8_t { Auto = AbstractFunctionConnector::Auto, AutoSync = AbstractFunctionConnector::AutoSync, Queued = AbstractFunctionConnector::Queued, Direct = AbstractFunctionConnector::Direct, QueuedSync = AbstractFunctionConnector::QueuedSync, Default = 0x80 };

  protected:
    Connection(AbstractFunctionConnector *, ConnectionType);
    virtual ~Connection() = 0;
    AbstractThread *thread_;
    ConnectionType type_;

  private:
    AbstractFunctionConnector *connector_;
    FunctionConnectionGuard *guarg_ = nullptr;
  };

  AbstractFunctionConnector(ConnectionType = Auto);

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
  Connection &operator()(T f, AbstractFunctionConnector::Connection::ConnectionType t = AbstractFunctionConnector::Connection::Default) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  }
  void operator()(Args... args) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const Connection *c : *reinterpret_cast<std::vector<Connection *> *>(&list_)) {
      std::function<void(Args...)> f = c->f;
      if (c->type_ == Connection::Direct || ((c->type_ == Connection::Auto || c->type_ == Connection::AutoSync) && c->thread_->id() == std::this_thread::get_id())) {
        f(args...);
        continue;
      }
      c->thread_->invokeMethod([f, args...]() { f(args...); }, c->type_ == Connection::AutoSync || c->type_ == Connection::QueuedSync);
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
