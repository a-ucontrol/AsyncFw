#include "core/LogStream.h"
#include "instance.hpp"

#ifdef EXTEND_INSTANCE_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

AbstractInstance::List::~List() { lsTrace() << instances.size(); }

void AbstractInstance::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.insert(it, _i);
  trace() << list.instances.size();
}

void AbstractInstance::remove(AbstractInstance *_i) {
  if (list.instances.empty()) {
    trace() << LogStream::Color::DarkRed << "empty";
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.erase(it);
  trace() << list.instances.size();
}

void AbstractInstance::destroyValues() {
  lsDebug() << list.instances.size();
  while (!list.instances.empty()) {
    list.instances.back()->destroyValue();
    list.instances.pop_back();
  }
}
