#include "Thread.h"
#include "LogStream.h"
#include "FunctionConnector.h"

#ifdef EXTEND_FUNCTIONCONNECTOR_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, uC_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

AbstractFunctionConnector::AbstractFunctionConnector(ConnectionType type) : defaultConnectionType(type), thread(AbstractThread::currentThread()) { trace() << this; }

AbstractFunctionConnector::~AbstractFunctionConnector() {
  std::lock_guard<std::mutex> lock(mutex);
  trace() << this << list_.size();
  for (Connection *c : list_) {
    c->connector_ = nullptr;
    delete c;
  }
}

AbstractFunctionConnector::Connection::Connection(AbstractFunctionConnector *connector, ConnectionType type) : connector_(connector) {
  uint8_t _type = (type != Default) ? type : connector->defaultConnectionType & ~0x10;
  if ((connector->defaultConnectionType & 0x10) && _type != (connector->defaultConnectionType & ~0x10)) {
    (logAlert() << "AbstractFunctionConnector: fixed connection type, throw exception...").flush();
    throw std::runtime_error("AbstractFunctionConnector: fixed connection type");
  }
  if (_type != Direct) {
    AbstractThread *_t = AbstractThread::currentThread();
    if ((_type != Auto && _type != AutoSync) || _t != connector->thread) thread_ = _t;
    if (_type == QueuedSync || (_type == AutoSync && thread_)) sync_ = true;
  }
  connector_->list_.push_back(this);
  trace() << LogStream::Color::Green << _type << connector->thread->name() << this << connector_ << connector_->list_.size();
}

AbstractFunctionConnector::Connection::~Connection() {
  if (guarg_) guarg_->c_ = nullptr;
  if (!connector_) return;
  std::lock_guard<std::mutex> lock(connector_->mutex);
  std::vector<Connection *>::iterator it = std::find(connector_->list_.begin(), connector_->list_.end(), this);
  connector_->list_.erase(it);
  trace() << this << connector_ << connector_->list_.size();
}

void AbstractFunctionConnector::Connection::invoke(const std::function<void()> &f) const { thread_->invokeMethod(f, sync_); }

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
    if (!c_->thread_) AbstractThread::currentThread()->invokeMethod([_p = c_]() { delete _p; });
    else { delete c_; }
  }
  c_ = _g.c_;
  _g.c_ = nullptr;
  if (c_) c_->guarg_ = this;
}

void FunctionConnectionGuardList::operator+=(FunctionConnectionGuard &&_g) { push_back(std::move(_g)); }
