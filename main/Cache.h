/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cache.h @brief The Cache class. */

//#include <mutex>
#include <../core/FunctionConnector.h>

namespace AsyncFw {
/** @class Cache @brief A thread-safe cache wrapper that automatically triggers an update signal when the value expires.
@details Implements the Stale-While-Revalidate pattern. When the cache expires, the first thread marks it as updating, temporarily releases the lock, and triggers an asynchronous update signal via a QueuedOnly policy. Subsequent readers during the update phase instantly receive the stale value without blocking, ensuring maximum high availability and zero downtime.
@tparam T The type of the cached data object. */
template <typename T>
class Cache {
public:
  /** @brief Constructs a new Cache object. @param timeout The Time-To-Live (TTL) duration for the cache in milliseconds. */
  Cache(int timeout) : timeout_(timeout) { expire_ = milliseconds_since_epoch(); }

  /** @brief Safely acquires the cached data view bound to its live thread lock.
  @details If the TTL has expired, it atomically drops the lock and dispatches the update() signal.
  @return An AbstractThread::Locked structure bundling the mutable reference to the data and its active lock. */
  AbstractThread::Locked<T &> acquire() {
    AbstractThread::Locked<T &> _locked {value_, mutex_};
    if (expire_ == 0 || expire_ > milliseconds_since_epoch()) return _locked;
    expire_ = 0;
    mutex_.unlock();
    update();
    mutex_.lock();
    return _locked;
  }

  /** @brief Securely stores a completely fresh object state inside the cache, extending its TTL. @param value The new value to store inside the cache. */
  void store(const T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire_ = timeout_ + milliseconds_since_epoch();
    value_ = value;
  }

  /** @brief Forces a cache invalidation and schedules an asynchronous refresh execution pipeline. @details Employs a double-trigger guard to ensure multiple concurrent refresh calls do not spawn duplicate background worker tasks. Safe to call from any external thread pool context. */
  void refresh() {
    {  //lock scope
      std::lock_guard<std::mutex> lock(mutex_);
      if (!expire_) return;
      expire_ = 0;
    }
    update();
  }

  /** @brief Signal triggered when the cached value becomes stale and requires updating.
  @note Enforces QueuedOnly delivery policy to guarantee that slot invokers never execute synchronously under the internal mutex, preventing any deadlocks. Protected access limits trigger capabilities exclusively to the owning Cache instance. */
  AsyncFw::FunctionConnector<>::Policy<AsyncFw::AbstractFunctionConnector::QueuedOnly>::Protected<Cache<T>> update;

private:
  int timeout_;
  uint64_t milliseconds_since_epoch() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  T value_;
  uint64_t expire_;
  std::mutex mutex_;
};
}  // namespace AsyncFw
