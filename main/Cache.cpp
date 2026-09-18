/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "../core/LogStream.h"
#include "Cache.h"

#define MILLISECONDS_SINCE_EPOCH (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())

AsyncFw::AbstractCache::AbstractCache(int timeout) : timeout_(timeout) {
  thread_ = AbstractThread::current();
  expire_ = MILLISECONDS_SINCE_EPOCH;
  lsTrace();
}

void AsyncFw::AbstractCache::update() { lsTrace(); }

AsyncFw::AbstractCache::~AbstractCache() { lsTrace(); }

void AsyncFw::AbstractCache::touch() {
  expire_ = timeout_ + MILLISECONDS_SINCE_EPOCH;
  lsDebug();
}

void AsyncFw::AbstractCache::refresh() {
  lsTrace();
  if (!expire_) return;
  update();
}

bool AsyncFw::AbstractCache::expired() { return expire_ <= MILLISECONDS_SINCE_EPOCH; }
