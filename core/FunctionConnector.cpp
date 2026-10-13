/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "LogStream.h"
#include "FunctionConnector.h"

#ifdef EXTEND_FUNCTIONCONNECTOR_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "extend_trace.hpp"

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionPolicy type) : connectionPolicy(type) { trace() << this; }

AbstractFunctionConnector::~AbstractFunctionConnector() {
  std::lock_guard<mutex_t> lock(mutex);
  trace() << this << list.size();
  for (Connection *c : list) {
    c->connector_ = nullptr;
    if (c->thread_) delete c;
  }
}

AbstractFunctionConnector::Connection::Connection(const AbstractFunctionConnector *connector, Type type) : thread_(AbstractThread::current()), type_(type != static_cast<Connection::Type>(0x80) ? type : static_cast<Type>(connector->connectionPolicy & ~0x10)), connector_(connector) {
  connector_->list.push_back(this);
  trace() << LogStream::Color::Green << static_cast<int>(type_) << thread_->name() << this << connector_ << connector_->list.size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guard_) guard_->connection_ = nullptr;
  if (!connector_) return;
  std::lock_guard<mutex_t> lock(connector_->mutex);
  connection_list_t::iterator it = std::find(connector_->list.begin(), connector_->list.end(), this);
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
#if 0
  AbstractThread *_thread = connection_->thread_;
  connection_->thread_ = nullptr;
  connection_->guard_ = nullptr;
  if (!_thread->invoke([_p = connection_]() { delete _p; })) { delete connection_; }
#else
  {  //lock scope
    std::lock_guard<AbstractFunctionConnector::mutex_t> lock(connection_->connector_->mutex);
    auto it = std::find(connection_->connector_->list.begin(), connection_->connector_->list.end(), connection_);
    connection_->connector_->list.erase(it);
    connection_->connector_ = nullptr;
  }
  delete connection_;
#endif
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
