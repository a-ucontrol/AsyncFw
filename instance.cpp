#include <algorithm>
#include "core/LogStream.h"
#include "core/console_msg.hpp"
#include "instance.hpp"

using namespace AsyncFw;

void AbstractInstance::List::destroy() {
  lsDebug() << list.size();
  std::for_each(list.rbegin(), list.rend(), [](AbstractInstance *_i) { _i->destroyValue(); });
}

AbstractInstance::List::~List() {
  console_msg_(std::string(__PRETTY_FUNCTION__)  + "  " + std::to_string(size()) + " 1");
  if (!empty()) {
    lsError() << "instance list not empty:" << size();
    destroy();
  }
  lsDebug() << size();
  console_msg_(std::string(__PRETTY_FUNCTION__)  + "  " + std::to_string(size()) + " 2");
}

void AbstractInstance::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.insert(it, _i);
}

void AbstractInstance::remove(AbstractInstance *_i) {
  if (list.empty()) {
    lsError("instance list empty");
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.erase(it);
}
