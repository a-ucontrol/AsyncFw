/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cache.h @brief The Cache class. */

#include "../core/FunctionConnector.h"

namespace AsyncFw {
/** @class Cache Cache.h <AsyncFw/Cache> @brief A thread-safe cache wrapper that emits an update signal when the value expires.
@details Implements the Stale-While-Revalidate pattern. When the TTL expires, the first caller atomically claims the update slot on expire_ (CAS in acquire(), exchange in refresh()) and emits the update signal. The actual refresh work is performed by subscribers of update; the Cache itself does not know which thread the work runs on. A subscriber may dispatch to another thread, run inline, or only log — the Cache only cares that touch() is eventually called. Subsequent readers during the update phase instantly receive the stale value if the subscriber dispatches elsewhere; if a subscriber runs inline on a caller thread, that caller blocks for the duration of the callback. Readers do not serialize against each other: value_ is guarded by a shared_mutex while expire_ is an atomic lifecycle flag. The update signal uses FunctionConnector<>::Protected<Cache<T>> with the Auto policy: subscribers connected from the emitting thread are invoked inline, others are queued to their own thread.
@tparam T The type of the cached data object.
@brief Example: @snippet Cache/main.cpp snippet */
template <typename T>
class Cache {
public:
  /** @brief Constructs a new Cache object.
  @details The cache starts in the expired state (expire_ == -1), so the first acquire() will claim an update and emit the update signal.
  @param timeout The Time-To-Live (TTL) duration for the cache in milliseconds. */
  Cache(int timeout) : timeout_(timeout), expire_(-1) {}
  /** @brief Acquires locked access to the cached value.
  @details The locking discipline is selected by the M template parameter: Mode::Lock::Exclusive (default) yields a mutable reference for the updater, Mode::Lock::Shared yields a const reference for readers. If the TTL has expired, the first caller atomically claims the update slot and emits the update signal before the lock is taken. The signal is emitted in the caller's thread; subscribers decide where the actual refresh runs (see FunctionConnector connection policies).
  @tparam M The locking discipline. Defaults to Mode::Lock::Exclusive. Mode::Lock::Mutex is not applicable to Cache. @return A LockData bundling the reference to the data and its active lock.
  @note When M == Mode::Lock::Exclusive, the caller is the updater: it must mutate value_ via the returned LockData and call touch() before that object goes out of scope. When M == Mode::Lock::Shared, the caller is a plain reader and must not call touch(). */
  template <Mode::Lock M = Mode::Lock::Exclusive>
  LockData<T &, M> acquire() {
    static_assert(M != Mode::Lock::Mutex, "Cache::acquire: Mode::Lock::Mutex is not applicable, use Exclusive or Shared");
    int64_t expected = expire_.load(std::memory_order_relaxed);
    if (expected != 0 && expected <= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) {
      if (expire_.compare_exchange_strong(expected, 0, std::memory_order_relaxed)) { update(); }
    }
    return {value_, mutex_};
  }
  /** @brief Stores a completely fresh object state inside the cache, extending its TTL.
  @details Takes the exclusive lock briefly to copy @p value, then commits the new deadline via touch(). Safe to call from any thread.
  @param value The new value to store inside the cache. */
  void store(const T &value) {
    {  //lock scope
      std::lock_guard<std::shared_mutex> lock(mutex_);
      value_ = value;
    }
    touch();
  }
  /** @brief Forces a cache invalidation and emits the update signal.
  @details Atomically exchanges expire_ to 0 and emits update() only if the previous value was non-zero. This guarantees a single winner among concurrent refresh calls. Does not require the mutex: expire_ is atomic and refresh does not touch value_. Safe to call from any thread.
  @note Unlike acquire(), refresh() ignores the current TTL and always invalidates: even fresh data is discarded and re-fetched.
  @warning Do not call refresh() from within an update() subscriber. With Auto policy, if the subscriber runs inline (same thread as the emitter), the re-entrant emit can deadlock on Cache::mutex_ if the subscriber still holds a LockData from acquire(). With queued delivery it causes an unbounded re-emit loop instead. */
  void refresh() {
    if (expire_.exchange(0, std::memory_order_relaxed) != 0) { update(); }
  }
  /** @brief Commits the update by extending the TTL from now.
  @details MUST be called by the update handler once the value has been refreshed. Safe to call while holding the exclusive lock returned from acquire(): expire_ is atomic and value_ is not touched.
  @warning Skipping this call leaves expire_ == 0 forever and the cache permanently stops refreshing. If the handler delegates work to another thread, that thread MUST call touch() on completion, including on failure paths. */
  void touch() { expire_.store(timeout_ + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed); }
  /** @brief Lock-free hint: true if the TTL has expired.
  @details Returns true when expire_ is 0 (an update is in flight) or the deadline is in the past. The result is a snapshot and may become stale immediately after the call. */
  bool expired() const {
    int64_t e = expire_.load(std::memory_order_relaxed);
    return !e || e <= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  /** @brief Signal emitted when the cache becomes stale and an update should be scheduled.
  @details Connect one or more handlers to refresh the value. The thread on which each handler runs is determined by the FunctionConnector connection policy at connect() time. With the default Auto policy, a handler is invoked inline when the emitter runs in the same thread as the subscriber, otherwise a task is queued to the subscriber's thread.
  @note Emission is restricted to the owning Cache instance (Protected variant). External code can only connect().
  @warning Subscribers must not call refresh() or re-enter acquire() while holding a LockData. Doing so deadlocks (inline delivery) or causes an unbounded re-emit loop (queued delivery). */
  AsyncFw::FunctionConnector<>::Protected<Cache<T>> update;

protected:
  int timeout_;                  ///< Cache expiration timeout duration in milliseconds.
  T value_;                      ///< Real inner cached data storage instance.
  std::atomic<int64_t> expire_;  ///< 0 while updating, else deadline timestamp in ms.
  std::shared_mutex mutex_;      ///< Guards value_ only. expire_ is a separate atomic lifecycle flag.
};
}  // namespace AsyncFw
