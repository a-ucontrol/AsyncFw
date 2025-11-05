#include "Thread.h"
#include "LogStream.h"
#include "FunctionConnector.h"

#ifdef EXTEND_CONNECTOR_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionType type) : connectionType(type) { trace() << this; }

AbstractThread *AbstractFunctionConnector::defaultConnectionThread_() {
  if (connectionType == DefaultDirect || connectionType == DirectOnly) return nullptr;
  AbstractThread *t = AbstractThread::currentThread();
  warning_if(!_t) << LogStream::Color::Red << "unknown thread, direct connection used by default";
  return t;
}

AbstractFunctionConnector::~AbstractFunctionConnector() {
  std::lock_guard<std::mutex> lock(mutex);
  trace() << this << list_.size();
  for (Connection *c : list_) {
    c->connector_ = nullptr;
    delete c;
  }
}

AbstractFunctionConnector::Connection::Connection(AbstractFunctionConnector *_connector, AbstractThread *_thread) : thread_(_thread), connector_(_connector) {
  if (_connector->connectionType == DirectOnly) {
    if (_thread) {
      logEmergency() << "AbstractFunctionConnector: default connection type: DirectOnly...";
      return;
    }
  }
  connector_->list_.push_back(this);
  trace() << this << connector_ << connector_->list_.size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guarg_) guarg_->c_ = nullptr;
  if (!connector_) return;
  std::lock_guard<std::mutex> lock(connector_->mutex);
  std::vector<Connection *>::iterator it = std::find(connector_->list_.begin(), connector_->list_.end(), this);
  connector_->list_.erase(it);
  trace() << this << connector_ << connector_->list_.size();
}

void AbstractFunctionConnector::Connection::queued(const std::function<void()> &f) const { thread_->invokeMethod(f); }

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

void FunctionConnectionGuard::disconnect() {
  if (!c_) return;
  delete c_;
  c_ = nullptr;
}

void FunctionConnectionGuard::disconnect_queued() {
  AbstractThread::currentThread()->invokeMethod([this]() { disconnect(); });
}

void FunctionConnectionGuard::operator=(AbstractFunctionConnector::Connection &_c) {
  if (c_) delete c_;
  c_ = &_c;
  c_->guarg_ = this;
}

void FunctionConnectionGuard::operator=(FunctionConnectionGuard &&_g) {
  if (c_) delete c_;
  c_ = _g.c_;
  _g.c_ = nullptr;
  if (c_) c_->guarg_ = this;
}

void FunctionConnectionGuardList::operator+=(FunctionConnectionGuard &&_g) { push_back(std::move(_g)); }
