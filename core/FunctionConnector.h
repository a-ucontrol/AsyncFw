#pragma once

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
  template <typename F, typename... Args>
  class QueuedTask : public AbstractThread::AbstractTask {
  public:
    QueuedTask(F *f, Args &...args) : f_(f), args_(args...) {}
    ~QueuedTask() { delete f_; }
    void invoke() override { std::apply(*f_, args_); }
    F *f_;
    std::tuple<Args...> args_;
  };
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
        (*c->f)(args...);
        continue;
      }
      if (c->type_ != Connection::AutoSync && c->type_ != Connection::QueuedSync) {
        AbstractThread::AbstractTask *_t = new QueuedTask(c->f->copy(), args...);
        if (!c->thread_->invokeTask(_t)) delete _t;
      } else {
        c->thread_->invokeMethod([c, &args...]() mutable { (*c->f)(args...); }, true);
      }
    }
  }

protected:
  class Connection : public AbstractFunctionConnector::Connection {
    friend class FunctionConnector<Args...>;

  public:
    template <typename T>
    Connection(T &_f, AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f(new Function(_f)) {}

  private:
    struct AbstractFunction {
      virtual AbstractFunction *copy() const = 0;
      virtual void operator()(Args &...) = 0;
      virtual ~AbstractFunction() = default;
    };
    template <typename T>
    struct Function : AbstractFunction {
      Function(T &_f) : f(std::move(_f)) {}
      Function(const Function *_f) : f(_f->f) {}
      ~Function() {}
      AbstractFunction *copy() const override { return new Function(this); }
      void operator()(Args &...args) override { f(std::forward<Args>(args)...); }
      T f;
    };
    ~Connection() override { delete f; }
    AbstractFunction *f;
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
    FunctionConnector<Args...>::Connection &operator()(T f, AbstractFunctionConnector::Connection::Type t = AbstractFunctionConnector::Connection::Default) {
      std::lock_guard<std::mutex> lock(FunctionConnector<Args...>::mutex);
#ifndef __clang_analyzer__
      return *new FunctionConnector<Args...>::Connection(f, this, t);
#endif
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
  operator bool() const { return c_; }

private:
  AbstractFunctionConnector::Connection *c_;
};

class FunctionConnectionGuardList : public std::vector<FunctionConnectionGuard> {
public:
  void operator+=(FunctionConnectionGuard &&);
};
}  // namespace AsyncFw
