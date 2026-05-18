/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \example FunctionConnector/main.cpp FunctionConnector example */

#include "AbstractThread.h"

namespace AsyncFw {

class FunctionConnectionGuard;

/*! \class AbstractFunctionConnector FunctionConnector.h <AsyncFw/FunctionConnector> \brief The AbstractFunctionConnector class provides the base functionality for FunctionConnector
  \exception std::runtime_error If the default connection type (AbstractFunctionConnector::ConnectionType) is DirectOnly, AutoOnly, or SyncOnly and there is a type mismatch when connecting (AbstractFunctionConnector::Connection::Type), the exception std::runtime_error("fixed connection type") will be raised.
*/
class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  enum ConnectionType : uint8_t { Auto = 0, Direct = 0x01, Queued = 0x02, Sync = 0x04, AutoOnly = 0x10, DirectOnly = 0x11, QueuedOnly = 0x12, SyncOnly = 0x14 };
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  public:
    enum Type : uint8_t { Auto = AbstractFunctionConnector::Auto, Direct = AbstractFunctionConnector::Direct, Queued = AbstractFunctionConnector::Queued, Sync = AbstractFunctionConnector::Sync, Default = 0x80 };

  protected:
    Connection(AbstractFunctionConnector *, Type);
    virtual ~Connection() = 0;
    AbstractThread *thread_;
    Type type_;

  private:
    AbstractFunctionConnector *connector_;
    FunctionConnectionGuard *guard_ = nullptr;
  };

  AbstractFunctionConnector(ConnectionType = Auto);

protected:
  template <typename F, typename... Args>
  class QueuedTask : public AbstractThread::AbstractTask {
  public:
    QueuedTask(F *f, Args &...args) : f_(f), args_(args...) {}
    ~QueuedTask() { delete f_; }
    void operator()() override { std::apply(*f_, args_); }
    F *f_;
    std::tuple<Args...> args_;
  };
  virtual ~AbstractFunctionConnector() = 0;
  std::vector<Connection *> list;
  ConnectionType defaultConnectionType;
  std::mutex mutex;
};

/*! \class FunctionConnector FunctionConnector.h <AsyncFw/FunctionConnector> \brief Обеспечивает соединение отправитель -> получатели. Получатели могут быть вызваны в своих потоках (по умолчанию).
 Другие инструментальные средства реализуют подобную коммуникацию с помощью коллбэков.
 Коллбэк - это указатель на функцию, поэтому, если вы хотите, чтобы функция обработки уведомила вас о каком-либо событии, вы передаете указатель на другую функцию (коллбэк) в функцию обработки.
 Затем функция обработки вызывает коллбэк, когда это необходимо.
 \brief FunctionConnector сообщение генерируются объектом, когда его внутреннее состояние изменяется каким-либо образом, который может представлять интерес других объектов.
 FunctionConnector сообщения являются функциями с открытым доступом и могут генерироваться из любого места, но рекомендуется генерировать их только из класса, который его определяет.
 Для того что бы только объект одного типа мог отправлять сообщения, следует использовать FunctionConnectorProtected.
\brief Example: \snippet FunctionConnector/main.cpp snippet */
template <typename... Args>
class FunctionConnector : public AbstractFunctionConnector {
public:
  using AbstractFunctionConnector::AbstractFunctionConnector;
  /*! \brief Подключиться */
  template <typename T>
  Connection &connect(T f, Connection::Type t = Connection::Default) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, t);
#endif
  } /*! \brief Подключиться */
  template <typename M, typename O>
  Connection &connect(M m, O *o, Connection::Type t = Connection::Default) {
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(m, o, this, t);
#endif
  }
  /*! \brief Отправить */
  void operator()(Args... args) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const Connection *c : *reinterpret_cast<std::vector<Connection *> *>(&list)) {
      if (!c->thread_) continue;
      if (c->type_ == Connection::Direct || (c->type_ != Connection::Queued && c->thread_->id() == std::this_thread::get_id())) {
        (c->f_)->invoke(args...);
        continue;
      }
      if (c->type_ != Connection::Sync) {
        AbstractThread::AbstractTask *_t = new QueuedTask(c->f_->copy(), args...);
        if (!c->thread_->invokeTask(_t)) delete _t;
      } else {
        c->thread_->invokeMethod([c, &args...]() { (c->f_)->invoke(args...); }, true);
      }
    }
  }

protected:
  class Connection : public AbstractFunctionConnector::Connection {
    friend class FunctionConnector<Args...>;

  public:
    template <typename T>
    Connection(T &f, AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f_(new Function(f)) {}
    template <typename M, typename O>
    Connection(M m, O *o, AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f_(new MemberFunction(m, o)) {}

  private:
    struct AbstractFunction : public AsyncFw::Function<Args &...>::template Abstract<void> {
      virtual void invoke(Args &...args) = 0;
      virtual AbstractFunction *copy() const = 0;
    };
    template <typename T>
    struct Function : AbstractFunction {
      Function(T &f) : f_(std::move(f)) {}
      Function(const Function *f) : f_(f->f_) {}
      void operator()(Args &...args) override { f_(std::forward<Args>(args)...); }
      void invoke(Args &...args) override { f_(args...); }
      AbstractFunction *copy() const override { return new Function(this); }
      T f_;
    };
    template <typename M, typename O>
    struct MemberFunction : AbstractFunction {
      MemberFunction(M m, O *o) : m_(m), o_(o) {}
      MemberFunction(const MemberFunction *f) : m_(f->m_), o_(f->o_) {}
      void operator()(Args &...args) override { (o_->*m_)(std::forward<Args>(args)...); }
      void invoke(Args &...args) override { (o_->*m_)(args...); }
      AbstractFunction *copy() const override { return new MemberFunction(this); }
      M m_;
      O *o_;
    };
    ~Connection() override { delete f_; }
    AbstractFunction *f_;
  };
};

/*! \class FunctionConnectorProtected FunctionConnector.h <AsyncFw/FunctionConnector> \brief Защищенный коннетор, отправитель может быть только один.
  \copybrief FunctionConnector\brief Example: \snippet FunctionConnector/main.cpp snippet */
template <typename F>
class FunctionConnectorProtected {
public:
  template <typename... Args>
  class Connector : private FunctionConnector<Args...> {
    friend F;

  public:
    using FunctionConnector<Args...>::FunctionConnector;
    using FunctionConnector<Args...>::connect;

  protected:
    using FunctionConnector<Args...>::operator();
  };
};

/*! \class FunctionConnectionGuard FunctionConnector.h <AsyncFw/FunctionConnector> \brief The FunctionConnectionGuard class.
 \brief При разрушении FunctionConnectionGuard соединение разрывается */
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
