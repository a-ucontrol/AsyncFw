#pragma once

#define FunctionConnector_FUNCTION
#ifndef FunctionConnector_FUNCTION
  #include <functional>
#endif
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
    enum Type : uint8_t { Auto = AbstractFunctionConnector::Auto, AutoSync = AbstractFunctionConnector::AutoSync, Queued = AbstractFunctionConnector::Queued, Direct = AbstractFunctionConnector::Direct, QueuedSync = AbstractFunctionConnector::QueuedSync, Default = 0x80 };

  protected:
    Connection(AbstractFunctionConnector *, Type);
    virtual ~Connection() = 0;
    AbstractThread *thread_;
    Type type_;

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
  Connection &operator()(T f, AbstractFunctionConnector::Connection::Type t = AbstractFunctionConnector::Connection::Default) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  }
  void operator()(Args... args) { send(args...); }

protected:
  void send(Args &...args) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const Connection *c : *reinterpret_cast<std::vector<Connection *> *>(&list_)) {
      if (c->type_ == Connection::Direct || ((c->type_ == Connection::Auto || c->type_ == Connection::AutoSync) && c->thread_->id() == std::this_thread::get_id())) {
        c->f(args...);
        continue;
      }
      if (c->type_ != Connection::AutoSync && c->type_ != Connection::QueuedSync) {
        c->thread_->invokeMethod([_f = c->f, ... _a = std::forward<Args>(args)]() mutable { _f(_a...); });
      } else {
        c->thread_->invokeMethod([c, &args...]() mutable { c->f(args...); }, true);
      }
    }
  }

private:
#ifdef FunctionConnector_FUNCTION
  class Function {
  public:
    template <typename T>
    Function(T f) : d_(new Data {new T(std::move(f)), &Function::invoke<T>, &Function::destroy_function<T>, 0}) {}
    ~Function() {
      if (!(d_->ref_)) {
        d_->df_(d_->f_);
        delete d_;
        return;
      }
      d_->ref_--;
    }
    Function(const Function &f) {
      d_ = f.d_;
      d_->ref_++;
    }
    void operator()(Args &...args) const { d_->i_(d_->f_, args...); }

  private:
    Function &operator=(const Function &) = delete;
    template <typename T>
    static void destroy_function(void *data) {
      delete static_cast<T *>(data);
    }
    template <typename T>
    static void invoke(void *data, Args &...args) {
      (*static_cast<T *>(data))(args...);
    }
    struct Data {
      void *f_;
      void (*i_)(void *, Args &...);
      void (*df_)(void *);
      int ref_;
    } *d_;
  };
#endif
  class Connection : public AbstractFunctionConnector::Connection {
    friend class FunctionConnector<Args...>;

  public:
#ifdef FunctionConnector_FUNCTION
    template <typename T>
    Connection(T f, AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f(f) {}
    Function f;
#else
    Connection(const std::function<void(Args...)> &f, AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f(f) {}
    std::function<void(Args...)> f;
#endif
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
    AbstractFunctionConnector::Connection &operator()(T f, AbstractFunctionConnector::Connection::Type t = AbstractFunctionConnector::Connection::Default) {
      return FunctionConnector<Args...>::operator()(f, t);
    }

  protected:
    void operator()(Args... args) { FunctionConnector<Args...>::send(args...); }
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
