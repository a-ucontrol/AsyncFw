/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "Task.h"
#include "core/LogStream.h"

AsyncFw::AbstractTask::AbstractTask() { lsTrace(); }
AsyncFw::AbstractTask::~AbstractTask() { lsTrace(); }

bool AsyncFw::AbstractTask::running() {
  lsDebug() << static_cast<bool>(running_);
  return running_;
}
