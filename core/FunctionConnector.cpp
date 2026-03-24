/*
Copyright (c) 2026 Alexandr Kuzmuk

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
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionType type) : defaultConnectionType(type) { trace() << this; }

AbstractFunctionConnector::~AbstractFunctionConnector() {
  std::lock_guard<std::mutex> lock(mutex);
  trace() << this << list.size();
  for (Connection *c : list) {
    c->connector_ = nullptr;
    delete c;
  }
}

AbstractFunctionConnector::Connection::Connection(AbstractFunctionConnector *connector, Type type) : connector_(connector) {
  type_ = (type != Default) ? type : static_cast<Type>(connector->defaultConnectionType & ~0x10);
  if ((connector->defaultConnectionType & 0x10) && type_ != (connector->defaultConnectionType & ~0x10)) {
    lsError().flush() << "fixed connection type, throw exception...";
    throw std::runtime_error("fixed connection type");
  }
  thread_ = AbstractThread::currentThread();
  std::vector<Connection *>::iterator it = std::lower_bound(connector_->list.begin(), connector_->list.end(), this, [](const Connection *c1, const Connection *c2) { return c1 < c2; });
  connector_->list.insert(it, this);
  trace() << LogStream::Color::Green << static_cast<int>(type_) << thread_->name() << this << connector_ << connector_->list.size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guarg_) guarg_->c_ = nullptr;
  if (!connector_) return;
  std::lock_guard<std::mutex> lock(connector_->mutex);
  std::vector<Connection *>::iterator it = std::lower_bound(connector_->list.begin(), connector_->list.end(), this, [](const Connection *c1, const Connection *c2) { return c1 < c2; });
  connector_->list.erase(it);
  trace() << this << connector_ << connector_->list.size();
}

FunctionConnectionGuard::FunctionConnectionGuard() { c_ = nullptr; }

FunctionConnectionGuard::FunctionConnectionGuard(FunctionConnectionGuard &&_g) {
  c_ = _g.c_;
  _g.c_ = nullptr;
  if (c_) c_->guarg_ = this;
}

FunctionConnectionGuard::FunctionConnectionGuard(AbstractFunctionConnector::Connection &_c) : c_(&_c) {
  c_->guarg_ = this;
  trace() << this;
}

FunctionConnectionGuard::~FunctionConnectionGuard() {
  if (!c_) return;
  delete c_;
  trace() << this;
}

void FunctionConnectionGuard::operator=(AbstractFunctionConnector::Connection &_c) {
  if (c_) delete c_;
  c_ = &_c;
  c_->guarg_ = this;
}

void FunctionConnectionGuard::operator=(FunctionConnectionGuard &&_g) {
  if (c_) {
    c_->guarg_ = nullptr;
    if (c_->thread_->id() != std::this_thread::get_id() || !c_->thread_->invokeMethod([_p = c_]() { delete _p; })) delete c_;
  }
  c_ = _g.c_;
  _g.c_ = nullptr;
  if (c_) c_->guarg_ = this;
}

void FunctionConnectionGuardList::operator+=(FunctionConnectionGuard &&_g) { push_back(std::move(_g)); }
