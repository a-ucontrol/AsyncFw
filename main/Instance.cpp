/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <algorithm>
#include "core/AbstractThread.h"
#include "core/LogStream.h"
#include "Instance.h"

using namespace AsyncFw;

struct AbstractInstance::Private {
  static inline class List : public std::vector<AbstractInstance *> {
    friend AbstractInstance;

  public:
    void append(AbstractInstance *);
    void remove(AbstractInstance *);

  private:
    ~List();
  } list __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 3)));
  std::string name;
};

void AbstractInstance::destroyValues() {
  lsTrace() << Private::list.size();
  int destroyed = 0;
  std::for_each(Private::list.rbegin(), Private::list.rend(), [&destroyed](AbstractInstance *_i) {
    if (_i->destroyValue()) destroyed++;
  });
  lsDebug() << LogStream::Color::DarkCyan << "destroyed:" << destroyed;
}

AbstractInstance::Private::List::~List() {
  if (!empty()) {
    lsError() << LogStream::Color::Red << "instance list not empty:" << size();
    destroyValues();
  }
  lsDebug() << LogStream::Color::DarkCyan << size();
}

void AbstractInstance::Private::List::append(AbstractInstance *_i) {
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.insert(it, _i);
  lsTrace() << _i->private_.name;
}

void AbstractInstance::Private::List::remove(AbstractInstance *_i) {
  lsTrace() << _i->private_.name;
  if (list.empty()) {
    lsError() << "instance list empty";
    return;
  }
  std::vector<AbstractInstance *>::iterator it = std::lower_bound(list.begin(), list.end(), _i, [](const AbstractInstance *i1, const AbstractInstance *i2) { return i1 < i2; });
  list.erase(it);
}

AbstractInstance::AbstractInstance(const std::string &name) : private_(*new Private) {
  private_.name = name;
  Private::list.append(this);
}

AbstractInstance::~AbstractInstance() {
  Private::list.remove(this);
  delete &private_;
}

void AbstractInstance::created() { lsDebug() << LogStream::Color::Cyan << private_.name; }

void AbstractInstance::destroing() { lsDebug() << LogStream::Color::Cyan << private_.name; }
