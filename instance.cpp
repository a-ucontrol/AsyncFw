#include <algorithm>
#include "ThreadPool.h"
#include "core/LogStream.h"
#include "instance.hpp"

using namespace AsyncFw;

void AbstractInstance::List::destroy() {
  lsDebug() << list.size();
  LogStream::ZonedTimeOffset::update();
  std::for_each(list.rbegin(), list.rend(), [](AbstractInstance *_i) { _i->destroyValue(); });
  AbstractThreadPool::List::destroy();
}

AbstractInstance::List::~List() {
  if (!empty()) {
    lsError() << "instance list not empty:" << size();
    destroy();
  }
  lsDebug() << size();
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
