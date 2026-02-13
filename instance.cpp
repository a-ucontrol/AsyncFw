#include "core/LogStream.h"
#include "instance.hpp"

using namespace AsyncFw;

AbstractInstance::List::~List() { lsDebug() << instances.size(); }

void AbstractInstance::List::append(AbstractInstance *p) {
  instances.push_back(p);
  lsDebug() << instances.size();
}

void AbstractInstance::List::destroyValues() {
  for (AbstractInstance *_i : instances) _i->destroyValue();
  lsDebug() << instances.size();
}
