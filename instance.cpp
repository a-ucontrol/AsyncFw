#include <algorithm>
#include "core/console_msg.hpp"
#include "instance.hpp"

using namespace AsyncFw;

AbstractInstance::List::~List() {
#ifndef LS_NO_DEBUG
  console_msg(__PRETTY_FUNCTION__ + ' ' + std::to_string(size()));
#endif
}

void AbstractInstance::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.insert(it, _i);
}

void AbstractInstance::remove(AbstractInstance *_i) {
  if (list.empty()) {
    console_msg(__PRETTY_FUNCTION__ + " Instance list empty");
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.erase(it);
}

void AbstractInstance::destroyValues() {
#ifndef LS_NO_DEBUG
  console_msg(__PRETTY_FUNCTION__ + ' ' + std::to_string(list.size()));
#endif
  std::for_each(list.rbegin(), list.rend(), [](AbstractInstance *_i) { _i->destroyValue(); });
}
