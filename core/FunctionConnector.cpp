/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "LogStream.h"
#include "FunctionConnector.h"

#ifdef EXTEND_FUNCTIONCONNECTOR_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace() \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionPolicy type) : connectionPolicy(type) { trace() << this; }

AbstractFunctionConnector::~AbstractFunctionConnector() {
  std::lock_guard<std::mutex> lock(mutex);
  trace() << this << list.size();
  for (Connection *c : list) {
    c->connector_ = nullptr;
    if (c->thread_) delete c;
  }
}

AbstractFunctionConnector::Connection::Connection(const AbstractFunctionConnector *connector, Type type) : thread_(AbstractThread::current()), type_(type != static_cast<Connection::Type>(0x80) ? type : static_cast<Type>(connector->connectionPolicy & ~0x10)), connector_(connector) {
  std::vector<Connection *>::iterator it = std::lower_bound(connector_->list.begin(), connector_->list.end(), this, [](const Connection *c1, const Connection *c2) { return c1 < c2; });
  connector_->list.insert(it, this);
  trace() << LogStream::Color::Green << static_cast<int>(type_) << thread_->name() << this << connector_ << connector_->list.size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guard_) guard_->connection_ = nullptr;
  if (!connector_) return;
  std::lock_guard<std::mutex> lock(connector_->mutex);
  std::vector<Connection *>::iterator it = std::lower_bound(connector_->list.begin(), connector_->list.end(), this, [](const Connection *c1, const Connection *c2) { return c1 < c2; });
  connector_->list.erase(it);
  trace() << this << connector_ << connector_->list.size();
}

FunctionConnectionGuard::FunctionConnectionGuard() : connection_(nullptr) {}

FunctionConnectionGuard::FunctionConnectionGuard(FunctionConnectionGuard &&guard) {
  connection_ = guard.connection_;
  guard.connection_ = nullptr;
  if (connection_) connection_->guard_ = this;
}

FunctionConnectionGuard::FunctionConnectionGuard(AbstractFunctionConnector::Connection &connection) : connection_(&connection) {
  connection_->guard_ = this;
  trace() << this;
}

FunctionConnectionGuard::~FunctionConnectionGuard() {
  if (connection_) destroyConnection();
  trace() << this;
}

void FunctionConnectionGuard::destroyConnection() {
  AbstractThread *_thread = connection_->thread_;
  connection_->thread_ = nullptr;
  connection_->guard_ = nullptr;
  if ((connection_->type_ != AbstractFunctionConnector::Connection::Direct && _thread->id() != std::this_thread::get_id()) || !_thread->invoke([_p = connection_]() { delete _p; })) { delete connection_; }
}

void FunctionConnectionGuard::operator=(AbstractFunctionConnector::Connection &connection) {
  if (connection_) destroyConnection();
  connection_ = &connection;
  connection_->guard_ = this;
}

void FunctionConnectionGuard::operator=(FunctionConnectionGuard &&guard) {
  if (connection_) destroyConnection();
  connection_ = guard.connection_;
  guard.connection_ = nullptr;
  if (connection_) connection_->guard_ = this;
}

void FunctionConnectionGuardList::operator+=(FunctionConnectionGuard &&guard) { push_back(std::move(guard)); }
