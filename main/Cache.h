/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cache.h @brief The Cache class. */

#include "../core/LockedData.h"
#include "../core/invocable.hpp"

#define MILLISECONDS_SINCE_EPOCH (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())

namespace AsyncFw {
/** @class Cache @brief A thread-safe cache wrapper that automatically triggers an update signal when the value expires.
@details Implements the Stale-While-Revalidate pattern. When the cache expires, the first thread marks it as updating, temporarily releases the lock, and triggers an asynchronous update signal via a QueuedOnly policy. Subsequent readers during the update phase instantly receive the stale value without blocking, ensuring maximum high availability and zero downtime.
@tparam T The type of the cached data object. */
template <typename T>
class Cache {
public:
  /** @brief Constructs a new Cache object. @tparam F Any callable type matching the signature void(). @param timeout The Time-To-Live (TTL) duration for the cache in milliseconds. @param function The update function/lambda to be executed upon cache expiration. */
  template <typename F>
  Cache(int timeout, F function) : timeout_(timeout), update_(new Invocable<void()>::Function(std::forward<F>(function))) {
    expire_ = MILLISECONDS_SINCE_EPOCH;
  }

  /** @brief Virtual-safe destructor ensuring polymorphically allocated callable cleanup. */
  virtual ~Cache() { delete update_; }

  /** @brief Safely acquires the cached data view bound to its live thread lock.
  @details If the TTL has expired, it atomically drops the lock and dispatches the update() signal.
  @return An AbstractThread::Locked structure bundling the mutable reference to the data and its active lock. */
  LockedData<T &> acquire() {
    LockedData<T &> _locked {value_, mutex_};
    if (expire_ == 0 || expire_ > MILLISECONDS_SINCE_EPOCH) return _locked;
    expire_ = 0;
    mutex_.unlock();
    (*update_)();
    mutex_.lock();
    return _locked;
  }

  /** @brief Securely stores a completely fresh object state inside the cache, extending its TTL. @param value The new value to store inside the cache. */
  void store(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_ = timeout_ + MILLISECONDS_SINCE_EPOCH;
    value_ = value;
  }

  /** @brief Forces a cache invalidation and schedules an asynchronous refresh execution pipeline. @details Employs a double-trigger guard to ensure multiple concurrent refresh calls do not spawn duplicate background worker tasks. Safe to call from any external thread pool context. */
  void refresh() {
    {  //lock scope
      std::lock_guard<std::mutex> lock(mutex_);
      if (!expire_) return;
      expire_ = 0;
    }
    (*update_)();
  }

protected:
  int timeout_;                         /**< Cache expiration timeout duration in milliseconds. */
  T value_;                             /**< Real inner cached data storage instance. */
  uint64_t expire_;                     /**< Timestamp in milliseconds denoting when the current cache frame expires. */
  std::mutex mutex_;                    /**< Core synchronization primitive for state isolation. */
  Invocable<void()>::Abstract *update_; /**< Encapsulated polymorphic callback executed to refresh data. */
};
}  // namespace AsyncFw
