/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "../core/LogStream.h"
#include "ApplicationNotifier.h"

using namespace AsyncFw;

Instance<ApplicationNotifier> ApplicationNotifier::instance_ {"ApplicationNotifier"};

ApplicationNotifier::ApplicationNotifier() { lsTrace(); }

AsyncFw::ApplicationNotifier::~ApplicationNotifier() {
  if (instance_.value == this) instance_.value = nullptr;
  lsTrace();
}
