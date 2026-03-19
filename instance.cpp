/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/Thread.h"
#include "core/LogStream.h"
#include "instance.hpp"

using namespace AsyncFw;

AbstractInstance::List AbstractInstance::list __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 2)));

void AbstractInstance::List::destroy() {
  lsDebug() << list.size();
  std::for_each(list.rbegin(), list.rend(), [](AbstractInstance *_i) { _i->destroyValue(); });
}

AbstractInstance::List::~List() {
  if (!empty()) {
    lsError() << LogStream::Color::Red << "instance list not empty:" << size();
    destroy();
  }
  lsDebug() << size();
}

void AbstractInstance::List::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.insert(it, _i);
  lsTrace() << _i->name;
}

void AbstractInstance::List::remove(AbstractInstance *_i) {
  lsTrace() << _i->name;
  if (list.empty()) {
    lsError() << "instance list empty";
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.erase(it);
}
