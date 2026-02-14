#include "core/LogStream.h"
#include "instance.hpp"

using namespace AsyncFw;

AbstractInstance::List::~List() { lsTrace() << instances.size(); }

void AbstractInstance::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.insert(it, _i);
  lsTrace() << list.instances.size();
}

void AbstractInstance::remove(AbstractInstance *_i) {
  if (list.instances.empty()) {
    lsTrace() << LogStream::Color::DarkRed << "empty";
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.erase(it);
  lsTrace() << list.instances.size();
}

void AbstractInstance::destroyValues() {
  lsDebug() << list.instances.size();
  for (AbstractInstance *_i : list.instances) _i->destroyValue();
  list.instances.clear();
}
