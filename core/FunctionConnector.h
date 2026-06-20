/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file FunctionConnector.h @brief The AbstractFunctionConnector, FunctionConnector, FunctionConnectorProtected, FunctionConnectionGuard and FunctionConnectionGuardList classes. */
/** @example FunctionConnector/main.cpp FunctionConnector example */

#include "AbstractThread.h"

namespace AsyncFw {
class FunctionConnectionGuard;
/** @class AbstractFunctionConnector FunctionConnector.h <AsyncFw/FunctionConnector> @brief The AbstractFunctionConnector class provides the base functionality for FunctionConnector. */
class AbstractFunctionConnector {
  friend FunctionConnectionGuard;

public:
  /** @enum ConnectionPolicy @brief Defines the default invocation behavior and validation constraints for the connector.
  @details This enumeration serves two purposes based on the value provided:
  1. **Default Value (Relaxed Modes):** If a subscriber connects without a specific Connection::Type, the connector falls back to the configured ConnectionPolicy (Auto, Direct, Queued, or Sync).
  2. **Strict Constraints (Only Modes):** Values with the Only suffix act as strict compile time validators. */
  enum ConnectionPolicy : uint8_t {
    Auto = 0,           ///< Default is Auto. Any explicit connection types are allowed.
    Direct = 0x01,      ///< Default is Direct. Any explicit connection types are allowed.
    Queued = 0x02,      ///< Default is Queued. Any explicit connection types are allowed.
    Sync = 0x04,        ///< Default is Sync. Any explicit connection types are allowed.
    DirectOnly = 0x11,  ///< Enforces Direct mode. Error if a subscriber explicitly requests type.
    QueuedOnly = 0x12,  ///< Enforces Queued mode. Error if a subscriber explicitly requests type.
    SyncOnly = 0x14     ///< Enforces Sync mode. Error if a subscriber explicitly requests type.
  };
  /** @class Connection FunctionConnector.h <AsyncFw/FunctionConnector> @brief Represents an active linkage between a sender's signal/connector and a specific receiver slot.
  @details The Connection class  representing a single active subscription. @n It stores crucial metadata required for dispatching events, including the target execution context AsyncFw::AbstractThread and the requested synchronization strategy Type. @n Instances of this class are typically allocated on the heap when a subscriber calls connect. Lifetime and memory cleanup are managed automatically by the framework, or can be tied to scopes via FunctionConnectionGuard. */
  class Connection {
    friend AbstractFunctionConnector;
    friend FunctionConnectionGuard;

  public:
    /** @enum Type @brief Specifies how a particular receiver slot should be invoked relative to the sender's thread.
    @details Defines the precise execution context and thread synchronization semantics for a specific connection instance during event dispatching. */
    enum Type : uint8_t {
      Auto = AbstractFunctionConnector::Auto,      ///< Evaluates the context at runtime. Uses Direct if the sender and receiver share the same thread ID, otherwise uses Queued.
      Direct = AbstractFunctionConnector::Direct,  ///< Invokes the receiver slot immediately inside the sender's thread. No context switching occurs.
      Queued = AbstractFunctionConnector::Queued,  ///< Posts a task containing a copy of the arguments into the receiver thread's event loop. Execution is asynchronous.
      Sync = AbstractFunctionConnector::Sync,      ///< Dispatches the invocation to the receiver's thread loop, but blocks the sender's thread until execution completes.
      Default = 0x80                               ///< Use the default ConnectionPolicy configuration defined by the connector.
    };
    AbstractThread *thread_;
    Type type_;

  protected:
    Connection(const AbstractFunctionConnector *, Type);
    virtual ~Connection() = 0;

  private:
    const AbstractFunctionConnector *connector_;
    FunctionConnectionGuard *guard_ = nullptr;
  };
  AbstractFunctionConnector(ConnectionPolicy = Auto);

protected:
  virtual ~AbstractFunctionConnector() = 0;
  ConnectionPolicy connectionPolicy;
  mutable std::vector<Connection *> list;
  mutable std::mutex mutex;
};

namespace internal {
template <AbstractFunctionConnector::ConnectionPolicy P, typename T, typename... Args>
class FunctionConnectorProtected;
template <AbstractFunctionConnector::ConnectionPolicy P = AbstractFunctionConnector::Auto, typename... Args>
class FunctionConnector : public AbstractFunctionConnector {
public:
  FunctionConnector() : AbstractFunctionConnector(P) {}
  template <Connection::Type T = Connection::Default, typename F>
  Connection &connect(F f) const {
    if constexpr ((P & 0x10) != 0) { static_assert(T == Connection::Default, "Error: Connection type mismatch!"); }
    constexpr typename Connection::Type type = (T != Connection::Default) ? T : static_cast<Connection::Type>(P & ~0x10);
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(f, this, type);
#endif
  }
  template <Connection::Type T = Connection::Default, typename M, typename O>
  Connection &connect(M m, O *o) const {
    if constexpr ((P & 0x10) != 0) { static_assert(T == Connection::Default, "Error: Connection type mismatch!"); }
    constexpr typename Connection::Type type = (T != Connection::Default) ? T : static_cast<Connection::Type>(P & ~0x10);
    std::lock_guard<std::mutex> lock(mutex);
#ifndef __clang_analyzer__
    return *new Connection(m, o, this, type);
#endif
  }
  void operator()(Args... args) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const AbstractFunctionConnector::Connection *c : list) {
      if (!c->thread_) continue;
      if (c->type_ == Connection::Direct || (c->type_ != Connection::Queued && c->thread_->id() == std::this_thread::get_id())) {
        (static_cast<const Connection *>(c)->f_)->invoke(args...);
        continue;
      }
      if (c->type_ != Connection::Sync) {
        AbstractThread::AbstractTask *_t = new QueuedTask(static_cast<const Connection *>(c)->f_, args...);
        if (!c->thread_->invokeTask(_t)) delete _t;
      } else {
        c->thread_->invoke([c, &args...]() { (static_cast<const Connection *>(c)->f_)->invoke(args...); }, true);
      }
    }
  }

protected:
  struct AbstractFunction : Invocable<void(Args &...)>::Abstract {
    /*
    Invokes the target callable, guaranteeing COPY semantics for arguments.
    This method is explicitly used during immediate signal dispatching (Direct/Sync) within the loop.
    It passes arguments as plain lvalues, forcing the compiler to generate independent copies for each distinct receiver slot.
    The default operator() cannot be used for this purpose because it uses std::forward, which casts lvalues to rvalues (triggering move semantics).
    If operator() were called in a loop, the very first receiver would empty out (steal resources from) the arguments, leaving subsequent subscribers with moved-from, empty objects.
    */
    virtual void invoke(Args &...args) = 0;
    std::atomic_int ref_ = 1;
  };
  struct QueuedTask : public AbstractThread::AbstractTask {
  public:
    QueuedTask(AbstractFunction *f, Args &...args) : f_(f), args_(args...) { f_->ref_++; }
    ~QueuedTask() {
      if (--f_->ref_ == 0) delete f_;
    }
    void operator()() override { std::apply(*f_, args_); }
    AbstractFunction *f_;
    std::tuple<std::decay_t<Args>...> args_;
  };

  class Connection : public AbstractFunctionConnector::Connection {
    friend FunctionConnector;

  public:
    template <typename F>
    Connection(F &f, const AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f_(new Function<F>(std::forward<F>(f))) {}
    template <typename M, typename O>
    Connection(M m, O *o, const AbstractFunctionConnector *c, Type t) : AbstractFunctionConnector::Connection(c, t), f_(new MemberFunction<M, O>(m, o)) {}

  private:
    template <typename F>
    struct Function : AbstractFunction {
      Function(F &&f) : f_(std::forward<F>(f)) {}
      void operator()(Args &...args) override { return f_(std::forward<Args>(args)...); }
      void invoke(Args &...args) override { this->f_(args...); }
      F f_;
    };
    template <typename M, typename O>
    struct MemberFunction : AbstractFunction {
      MemberFunction(M m, O *o) : m_(m), o_(o) {}
      void operator()(Args &...args) override { return (o_->*m_)(std::forward<Args>(args)...); }
      void invoke(Args &...args) override { (this->o_->*this->m_)(args...); }
      M m_;
      O *o_;
    };
    ~Connection() override {
      if (--f_->ref_ == 0) delete f_;
    }
    AbstractFunction *f_;
  };
};
// A protected connector where only a single designated sender can emit messages.
template <AbstractFunctionConnector::ConnectionPolicy P, typename T, typename... Args>
class FunctionConnectorProtected : private FunctionConnector<P, Args...> {
  friend T;

public:
  using FunctionConnector<P, Args...>::connect;

protected:
  using FunctionConnector<P, Args...>::operator();
};
}  // namespace internal

/** @class FunctionConnector FunctionConnector.h <AsyncFw/FunctionConnector> @brief Provides a thread-safe, publisher-subscriber mechanism to establish sender-to-receivers connections.
@details Receivers can be automatically invoked in their own designated target threads (the default behavior). Other frameworks typically implement this asynchronous communication pattern using bare callbacks or function pointers. @n FunctionConnector messages (signals) are generated by an object when its internal state changes in a way that might be of interest to other application components. While these connectors are public functions and can technically be emitted from anywhere, it is highly recommended to emit them exclusively from within the class that defines them. @n To strictly restrict message emission to only one specific object type and enforce encapsulation, use the FunctionConnector::Protected variant instead.
@par The class supports three declaration variants depending on encapsulation and validation needs:
1. **Basic Variant (Auto policy):** @code AsyncFw::FunctionConnector<int, std::string> connector; @endcode
2. **Protected Variant (Auto policy, emission restricted to Sender):** @code AsyncFw::FunctionConnector<int, std::string>::Protected<Sender> connector; @endcode
3. **Strict Compile-Time Variant (Emission restricted to Sender and strict connection type validation):** @code AsyncFw::FunctionConnector<int, std::string>::Policy<AsyncFw::AbstractFunctionConnector::DirectOnly>::Protected<Sender> connector; @endcode
@note All subscription registrations and signal emissions are fully synchronized internally.
@brief Example: @snippet FunctionConnector/main.cpp snippet */
template <typename... Args>
class FunctionConnector : public internal::FunctionConnector<AbstractFunctionConnector::Auto, Args...> {
public:
#ifdef __AsyncFw_DOC__  // for doxygen
  /** @brief Establishes a connection to a callable target.
  @details Allows registering lambdas, static functions, and functors. @n If the connector has a strict policy (like DirectOnly), requesting an incompatible type at compile time will trigger a static_assert error.
  @note **Usage styles:**
  @code
  // Style 1: Use default policy configuration
  sender->connector.connect(lambda);
  // Style 2: Explicitly enforce connection behavior via template argument
  sender->connector.connect<AsyncFw::AbstractFunctionConnector::Connection::Queued>(lambda);
  @endcode
  @tparam T The thread execution context strategy. @param f The callable target object representing the receiver slot. @return Reference to the managed Connection. */
  template <AbstractFunctionConnector::Connection::Type T = static_cast<AbstractFunctionConnector::Connection::Type>(0x80), typename F>
  AbstractFunctionConnector::Connection &connect(F f) const;

  /** @brief Connects a specific class member method.
  @note **Usage styles:**
  @code
  // Style 1: Use default policy configuration
  sender->connector.connect(&MethodConnectionExample::method, &object);
  // Style 2: Explicitly enforce connection behavior via template argument
  sender->connector.connect<Connection::Direct>(&MethodConnectionExample::method, &object);
  @endcode
  @tparam T The thread execution context strategy. @param m The pointer to the member function of the target object. @param o The pointer to the specific instance of the object containing the member function. @return Reference to the managed Connection. */
  template <AbstractFunctionConnector::Connection::Type T = static_cast<AbstractFunctionConnector::Connection::Type>(0x80), typename M, typename O>
  AbstractFunctionConnector::Connection &connect(M m, O *o) const;

  /** @brief Emits/Sends the signal, notifying all connected receivers.
  @details Iterates through the delivery list and dispatches arguments to each slot based on their configured Connection::Type.
  @param args The pack of arguments to forward to all subscribed receivers. */
  void operator()(Args... args) const;
#endif
  /** @brief A protected connector variant where only a single designated owner class can emit messages.
  @details Enhances encapsulation by preventing external code from triggering the connector. External code can only subscribe to notifications. Uses the default ConnectionPolicy::Auto.
  @tparam T The owner class (emitter) that is granted exclusive access to message emission. */
  template <typename T>
  using Protected = internal::FunctionConnectorProtected<AbstractFunctionConnector::Auto, T, Args...>;
  /** @struct Policy @brief A configuration interface to enforce a specific connection policy at compile time.
  @tparam P The enforced strict policy (e.g., Direct, QueuedOnly, etc.). */
  template <AbstractFunctionConnector::ConnectionPolicy P>
  struct Policy : internal::FunctionConnector<P, Args...> {
    template <typename T>
    using Protected = internal::FunctionConnectorProtected<P, T, Args...>;
  };
};

/** @class FunctionConnectionGuard FunctionConnector.h <AsyncFw/FunctionConnector> @brief An RAII guard that automatically manages the lifecycle of a FunctionConnector connection.
@details This class provides automated connection management using the Resource Acquisition Is Initialization (RAII) idiom. When a FunctionConnectionGuard goes out of scope or is destroyed, it automatically disconnects and cleans up its associated connection. It supports move semantics, allowing the guard to be transferred between scopes or stored inside containers. It explicitly disables copying.
@note The destruction of the connection is thread-safe. If the connection type is asynchronous and managed by another thread, the actual deletion of the connection object is safely dispatched to that specific thread event loop.
@brief Example: @snippet snippet.dox FunctionConnectorGuard */
class FunctionConnectionGuard {
  friend AbstractFunctionConnector::Connection;

public:
  /** @brief Constructs an empty, uninitialized connection guard. */
  FunctionConnectionGuard();
  /** @brief Move constructor. Transfers connection ownership from another guard */
  FunctionConnectionGuard(FunctionConnectionGuard &&);
  /** @brief Constructs a guard and binds it to an active connection. @param connection Reference to the connection to be managed. */
  FunctionConnectionGuard(AbstractFunctionConnector::Connection &);
  /** @brief Destructor. Automatically triggers disconnection and frees connection resources. */
  ~FunctionConnectionGuard();
  /** @brief Assigns a new active connection to this guard. Disconnects  previously managed connection. @param connection Reference to the new connection. */
  void operator=(AbstractFunctionConnector::Connection &);
  /** @brief Move assignment operator. Safely releases current connection and takes ownership of another. @param guard Rvalue reference to the source guard. */
  void operator=(FunctionConnectionGuard &&);
  /** @brief Checks if the guard is currently managing an active connection. @return True If managing an active connection. */
  operator bool() const { return connection_; }

private:
  AbstractFunctionConnector::Connection *connection_;
  void destroyConnection();
};

/** @class FunctionConnectionGuardList FunctionConnector.h <AsyncFw/FunctionConnector> @brief A helper container designed to manage multiple FunctionConnectionGuard lifecycles at once.
@details Inherits from std::vector<FunctionConnectionGuard>. It provides a convenient way to aggregate multiple connection guards within a single scope (e.g., inside a controller or view class). When the list goes out of scope, all registered connections are safely and automatically disconnected.
@brief Example: @snippet snippet.dox FunctionConnectorGuardList */
class FunctionConnectionGuardList : public std::vector<FunctionConnectionGuard> {
public:
  /** @brief Appends a moving connection guard to the management list. */
  void operator+=(FunctionConnectionGuard &&);
};
}  // namespace AsyncFw
