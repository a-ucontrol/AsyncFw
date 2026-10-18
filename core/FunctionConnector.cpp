/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "LogStream.h"
#include "FunctionConnector.h"

#ifdef EXTEND_FUNCTIONCONNECTOR_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "extend_trace.hpp"

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionPolicy type) : connectionPolicy(type), list(new List) { trace() << this; }

AbstractFunctionConnector::~AbstractFunctionConnector() {
  {  // lock scope
    std::lock_guard<mutex_t> lock(list->mutex);
    trace() << this << list->size();
    for (Connection *c : *list) {
      c->list_ = nullptr;
      delete c;
    }
  }
  if (list->ref_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete list;
}

AbstractFunctionConnector::Connection::Connection(List *list, Type type) : thread_(AbstractThread::current()), type_(type), list_(list) {
  list->push_back(this);
  trace() << LogStream::Color::Green << static_cast<int>(type_) << thread_->name() << this << list << list->size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guard_) guard_->connection_ = nullptr;
  if (!list_) return;
  std::lock_guard<mutex_t> lock(list_->mutex);
  connection_list_t::iterator it = std::find(list_->begin(), list_->end(), this);
  list_->erase(it);
  trace() << this << list_ << list_->size();
}

FunctionConnectionGuard::FunctionConnectionGuard() : connection_(nullptr), list_(nullptr) {}

FunctionConnectionGuard::FunctionConnectionGuard(FunctionConnectionGuard &&guard) : connection_(guard.connection_), list_(guard.list_) {
  guard.connection_ = nullptr;
  guard.list_ = nullptr;
  if (connection_) connection_->guard_ = this;
}

FunctionConnectionGuard::FunctionConnectionGuard(AbstractFunctionConnector::Connection &connection) : connection_(&connection), list_(connection.list_) {
  list_->ref_.fetch_add(1, std::memory_order_relaxed);
  connection_->guard_ = this;
  trace() << this;
}

FunctionConnectionGuard::~FunctionConnectionGuard() {
  if (list_) destroyConnection();
  trace() << this;
}

void FunctionConnectionGuard::destroyConnection() {
  {  // lock scope
    std::lock_guard<AbstractFunctionConnector::mutex_t> lock(list_->mutex);
    if (connection_) {
      AbstractFunctionConnector::List::iterator it = std::find(list_->begin(), list_->end(), connection_);
      list_->erase(it);
      connection_->list_ = nullptr;
      delete connection_;
    }
  }
  if (list_->ref_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete list_;
}

void FunctionConnectionGuard::operator=(AbstractFunctionConnector::Connection &connection) {
  if (list_) destroyConnection();
  list_ = connection.list_;
  list_->ref_.fetch_add(1, std::memory_order_relaxed);
  connection_ = &connection;
  connection_->guard_ = this;
}

void FunctionConnectionGuard::operator=(FunctionConnectionGuard &&guard) {
  if (list_) destroyConnection();
  connection_ = guard.connection_;
  list_ = guard.list_;
  guard.connection_ = nullptr;
  guard.list_ = nullptr;
  if (connection_) connection_->guard_ = this;
}

void FunctionConnectionGuardList::operator+=(FunctionConnectionGuard &&guard) { push_back(std::move(guard)); }
