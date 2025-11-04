#pragma once

#include <functional>
#include <mutex>

namespace AsyncFw {
class AbstractThread;
class FunctionConnectionGuard;

class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  enum ConnectionType : uint8_t { DefaultQueued, DefaultDirect, QueuedOnly, DirectOnly };
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  protected:
    Connection(AbstractFunctionConnector *, AbstractThread *);
    virtual ~Connection() = 0;
    void queued(const std::function<void(void)> &) const;
    AbstractThread *thread_;

  private:
    AbstractFunctionConnector *connector_;
    FunctionConnectionGuard *guarg_ = nullptr;
  };

  AbstractFunctionConnector(ConnectionType = DefaultQueued);

protected:
  virtual ~AbstractFunctionConnector() = 0;
  AbstractThread *defaultConnectionThread_();
  std::vector<Connection *> list_;
  ConnectionType connectionType;
  std::mutex mutex;
};

template <typename... Args>
class FunctionConnector : public AbstractFunctionConnector {
public:
  using AbstractFunctionConnector::AbstractFunctionConnector;
  template <typename T>
  Connection &operator()(T f, AbstractThread *t) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  }
  template <typename T>
  Connection &operator()(T f) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, defaultConnectionThread_());
#endif
  }
  void operator()(Args... args) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const AbstractFunctionConnector::Connection *c : list_) {
      std::function<void(Args...)> f = static_cast<const Connection *>(c)->f;
      if (static_cast<const Connection *>(c)->thread_) static_cast<const Connection *>(c)->queued([f, args...]() { f(args...); });
      else { f(args...); }
    }
  }

private:
  class Connection : public AbstractFunctionConnector::Connection {
    friend class FunctionConnector<Args...>;

  public:
    Connection(const std::function<void(Args...)> &_f, AbstractFunctionConnector *_c, AbstractThread *_t) : AbstractFunctionConnector::Connection(_c, _t), f(_f) {}
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
    AbstractFunctionConnector::Connection &operator()(T f, AbstractThread *t) {
      return FunctionConnector<Args...>::operator()(f, t);
    }
    template <typename T>
    AbstractFunctionConnector::Connection &operator()(T f) {
      return FunctionConnector<Args...>::operator()(f, AbstractFunctionConnector::defaultConnectionThread_());
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
  void disconnect();
  void disconnect_queued();
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
