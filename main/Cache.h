/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cache.h @brief The Cache class. */

#include "../core/AbstractThread.h"

namespace AsyncFw {

class AbstractCache {
protected:
  /** @brief Forces a cache invalidation and schedules an asynchronous refresh. Caller must hold mutex_. */
  virtual void refresh();
  /** @brief Extends the cache TTL without changing the cached value. Caller must hold mutex_. */
  virtual void touch();
  /** @brief Returns true if the TTL has expired. Caller must hold mutex_. */
  virtual bool expired();
  AbstractCache(int timeout);
  virtual ~AbstractCache() = 0;
  /** @brief Schedules the update callback. Caller must hold mutex_. */
  virtual void update();
  int timeout_;             ///< Cache expiration timeout duration in milliseconds.
  int64_t expire_;          ///< Timestamp in milliseconds when the current cache frame expires. 0 while an update is in progress.
  std::mutex mutex_;        ///< Core synchronization primitive for state isolation.
  AbstractThread *thread_;  ///< Execution thread managing this cache. Must not be null.
};

/** @class Cache @brief A thread-safe cache wrapper that automatically triggers an update signal when the value expires.
@details Implements the Stale-While-Revalidate pattern. When the cache expires, the first reader marks it for updating and schedules the update callback via the owning thread's task queue. Subsequent data requests arriving during the update process immediately receive the stale value without blocking, ensuring maximum availability and zero downtime.
@tparam T The type of the cached data object.
@brief Example: @snippet Cache/main.cpp snippet */
template <typename T>
class Cache : public AbstractCache {
public:
  /** @brief Constructs a new Cache object. @tparam F Any callable type matching the signature void(). @param timeout The Time-To-Live (TTL) duration for the cache in milliseconds. @param update The update function/lambda to be executed upon cache expiration. */
  template <typename F>
  Cache(int timeout, F update) : AbstractCache(timeout), update_(new Invocable<void()>::Function(std::forward<F>(update))) {}
  /** @brief Destructor that releases the polymorphically allocated callable. */
  ~Cache() { delete update_; }
  /** @brief Safely acquires the cached data view bound to its live thread lock.
  @details If the TTL has expired, the first reader marks the cache as updating and schedules the update callback via the owning thread's task queue. The lock is held for the duration of acquire() only; the callback runs later in the thread's event loop. Subsequent readers during the update phase instantly receive the stale value.
  @return A LockData bundle holding the mutable reference to the data and its active lock. */
  LockData<T &> acquire() {
    LockData<T &> _locked {value_, mutex_};
    if (expire_ == 0 || !AbstractCache::expired()) return _locked;
    update();
    return _locked;
  }
  /** @brief Securely stores a completely fresh object state inside the cache, extending its TTL. @param value The new value to store inside the cache. */
  void store(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    AbstractCache::touch();
    value_ = value;
  }
  /** @brief Forcibly invalidates the cache and schedules an async refresh. Public entry point; takes the lock. */
  void refresh() override {
    std::lock_guard<std::mutex> lock(mutex_);
    AbstractCache::refresh();
  }
  /** @brief Extends the TTL without changing the value. Public entry point; schedules on the owning thread. */
  void touch() override {
    thread_->invoke([this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      AbstractCache::touch();
    });
  }
  /** @brief Returns true if the TTL has expired. Public entry point; takes the lock. */
  bool expired() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return AbstractCache::expired();
  }

protected:
  /** @brief Schedules the update callback. Caller must hold mutex. */
  void update() override {
    AbstractCache::update();
    if (thread_->invoke([this]() { (*update_)(); })) expire_ = 0;
  }
  T value_;                              ///< Real inner cached data storage instance.
  Invocable<void()>::Abstract *update_;  ///< Invocable for data updates.
};
}  // namespace AsyncFw
