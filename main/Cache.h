/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cache.h @brief The Cache class. */

#include "../core/AbstractThread.h"
#include "../core/invocable.hpp"
#include "SharedLockData.h"

#define MILLISECONDS_SINCE_EPOCH (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())

namespace AsyncFw {
/** @class Cache @brief A thread-safe cache wrapper that automatically triggers an update signal when the value expires.
@details Implements the Stale-While-Revalidate pattern. When the TTL expires, the first caller atomically claims the update slot on expire_ (CAS in acquire(), exchange in refresh()) and dispatches the user callback asynchronously through thread_->invoke(). Subsequent readers during the update phase instantly receive the stale value without blocking on the updater. Readers do not serialize against each other: value_ is guarded by a shared_mutex while expire_ is an atomic lifecycle flag.
@tparam T The type of the cached data object.
@brief Example: @snippet Cache/main.cpp snippet */
template <typename T>
class Cache {
public:
  /** @brief Constructs a new Cache object. @tparam F Any callable type matching the signature void(). @param timeout The Time-To-Live (TTL) duration for the cache in milliseconds. @param function The update function/lambda to be executed upon cache expiration. Runs asynchronously on AbstractThread::current() captured at construction time. */
  template <typename F>
  Cache(int timeout, F function) : timeout_(timeout), update_(new Invocable<void()>::Function(std::forward<F>(function))) {
    thread_ = AbstractThread::current();
    expire_ = MILLISECONDS_SINCE_EPOCH;
  }

  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;
  Cache(Cache &&) = delete;
  Cache &operator=(Cache &&) = delete;

  /** @brief Destructor that releases the polymorphically allocated callable. */
  ~Cache() { delete update_; }
  /** @brief Acquires locked access to the cached value.
  @details The locking discipline is selected by the M template parameter: Mode::Lock::Exclusive (default) yields a mutable reference for the updater, Mode::Lock::Shared yields a const reference for readers. If the TTL has expired, the first caller atomically claims the update slot and dispatches the update callback asynchronously on thread_ before the lock is taken.
  @tparam M The locking discipline. Defaults to Mode::Lock::Exclusive. @return A SharedLockData bundling the reference to the data and its active lock.
  @note When M == Mode::Lock::Exclusive, the caller is the updater: it must mutate value_ via the returned SharedLockData and call touch() before that object goes out of scope. When M == Mode::Lock::Shared, the caller is a plain reader and must not call touch(). */
  template <Mode::Lock M = Mode::Lock::Exclusive>
  SharedLockData<T &, M> acquire() {
    int64_t expected = expire_.load(std::memory_order_relaxed);
    if (expected != 0 && expected <= MILLISECONDS_SINCE_EPOCH) {
      if (expire_.compare_exchange_strong(expected, 0, std::memory_order_relaxed)) {
        thread_->invoke([this]() { (*update_)(); });
      }
    }
    return {value_, mutex_};
  }
  /** @brief Securely stores a completely fresh object state inside the cache, extending its TTL. @param value The new value to store inside the cache. */
  void store(const T &value) {
    {  //lock scope
      std::lock_guard<std::shared_mutex> lock(mutex_);
      value_ = value;
    }
    touch();
  }
  /** @brief Forces a cache invalidation and schedules an asynchronous refresh.
  @details Atomically exchanges expire_ to 0 and dispatches the update callback only if the previous value was non-zero. This guarantees a single winner among concurrent refresh calls and callers arriving while an update is already in flight. Does not require the mutex: expire_ is atomic and refresh does not touch value_. Safe to call from any thread.
  @note Unlike acquire(), refresh() ignores the current TTL and always invalidates: even fresh data is discarded and re-fetched. */
  void refresh() {
    if (expire_.exchange(0, std::memory_order_relaxed) != 0) {
      thread_->invoke([this]() { (*update_)(); });
    }
  }
  /** @brief Commits the update by extending the TTL from now.
  @note MUST be called by the update handler before it releases the lock acquired via acquire(). Skipping it leaves expire_ == 0 forever and the cache stops refreshing. Safe to call while holding the exclusive lock returned from acquire(): expire_ is atomic and value_ is not touched. */
  void touch() { expire_.store(timeout_ + MILLISECONDS_SINCE_EPOCH, std::memory_order_relaxed); }
  /** @brief Lock-free hint: true if the TTL has expired. */
  bool expired() const {
    int64_t e = expire_.load(std::memory_order_relaxed);
    return !e || e <= MILLISECONDS_SINCE_EPOCH;
  }

protected:
  AbstractThread *thread_;               ///< Owning event loop / executor captured at construction.
  int timeout_;                          ///< Cache expiration timeout duration in milliseconds.
  T value_;                              ///< Real inner cached data storage instance.
  std::atomic<int64_t> expire_;          ///< 0 while updating, else deadline timestamp in ms.
  std::shared_mutex mutex_;              ///< Guards value only, expire_ is atomic.
  Invocable<void()>::Abstract *update_;  ///< Encapsulated polymorphic callback executed to refresh data.
};
}  // namespace AsyncFw
