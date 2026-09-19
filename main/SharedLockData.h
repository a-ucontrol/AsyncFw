/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file SharedLockData.h @brief The SharedLockData class. */

#include <shared_mutex>

namespace AsyncFw {

namespace Mode {
/** @enum Lock SharedLockData.h <AsyncFw/SharedLockData> @brief Selects the locking discipline for a SharedLockData carrier. @details Exclusive mirrors std::shared_mutex::lock; Shared mirrors std::shared_mutex::lock_shared. */
enum class Lock {
  Exclusive,  ///< Exclusive (write) lock. Blocks all readers until released.
  Shared      ///< Shared (read) lock. Multiple readers may hold it simultaneously.
};
}  // namespace Mode

/** @struct SharedLockData SharedLockData.h <AsyncFw/SharedLockData> @brief A zero-copy RAII carrier that couples a synchronized resource view with its active shared_mutex lock guard.
@details Enforces compile-time checks to ensure that the underlying type is strictly a reference or a pointer, effectively preventing accidental heavy memory allocations or copying during multi-threaded data sharing. The locking discipline is selected at compile time via the M template parameter.
@tparam T The resource type declaration, which must be either a reference or a pointer. @tparam M The locking discipline. Defaults to Mode::Lock::Exclusive, mirroring std::shared_mutex::lock. */
template <typename T, Mode::Lock M = Mode::Lock::Exclusive>
struct SharedLockData {
  static_assert(std::is_reference_v<T> || std::is_pointer_v<T>, "SharedLockData<T> error: T must be either a reference or a pointer to avoid copying!");

  using _T = std::conditional_t<M == Mode::Lock::Exclusive, std::remove_reference_t<T>, const std::remove_reference_t<T>>;

  /** @brief Constructs the carrier by binding a raw pointer target and acquiring the lock.
  @details Acquires an exclusive lock for Mode::Lock::Exclusive, or a shared lock for Mode::Lock::Shared. */
  SharedLockData(_T *data, std::shared_mutex &mutex) : data_(data), mutex_(&mutex) {
    if constexpr (M == Mode::Lock::Exclusive) mutex_->lock();
    else mutex_->lock_shared();
  }
  /** @brief Constructs the carrier by taking the address of an lvalue reference. */
  SharedLockData(_T &data, std::shared_mutex &mutex) : SharedLockData(&data, mutex) {}
  /** @brief Move constructor that transfers the critical section ownership from an rvalue SharedLockData state.
  @details The source is left without lock ownership and with a null data pointer. */
  SharedLockData(SharedLockData &&locked) : data_(locked.data_), mutex_(locked.mutex_) {
    locked.data_ = nullptr;
    locked.mutex_ = nullptr;
  }
  /** @brief Destructor that safely releases the acquired lock if this instance still retains its ownership. */
  ~SharedLockData() {
    if (!mutex_) return;
    if constexpr (M == Mode::Lock::Exclusive) mutex_->unlock();
    else mutex_->unlock_shared();
  }
  /** @brief Indirection operator that returns a pointer to the protected inner resource.
  @details Returns a mutable pointer for Mode::Lock::Exclusive, or a const pointer for Mode::Lock::Shared. */
  _T *operator->() const { return data_; }
  /** @brief Indirection operator that returns a reference to the protected inner resource.
  @details Returns a mutable reference for Mode::Lock::Exclusive, or a const reference for Mode::Lock::Shared. */
  _T &operator*() const { return *data_; }

protected:
  _T *data_;                  ///< Pointer to the protected inner resource.
  std::shared_mutex *mutex_;  ///< Pointer to the synchronization primitive, cleared upon moving.
};
}  // namespace AsyncFw
