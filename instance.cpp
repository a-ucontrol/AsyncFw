#include <algorithm>
#include "core/console_msg.hpp"
#include "instance.hpp"

using namespace AsyncFw;

AbstractInstance::List::~List() {
#ifndef LS_NO_DEBUG
  console_msg(__PRETTY_FUNCTION__ + ' ' + std::to_string(instances.size()));
#endif
}

void AbstractInstance::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.insert(it, _i);
}

void AbstractInstance::remove(AbstractInstance *_i) {
  if (list.instances.empty()) {
    console_msg(__PRETTY_FUNCTION__ + " Instance list empty");
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.instances.begin(), list.instances.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.instances.erase(it);
}

void AbstractInstance::destroyValues() {
#ifndef LS_NO_DEBUG
  console_msg(__PRETTY_FUNCTION__ + ' ' + std::to_string(list.instances.size()));
#endif
  std::for_each(list.instances.rbegin(), list.instances.rend(), [](AbstractInstance *_i) { _i->destroyValue(); });
}
