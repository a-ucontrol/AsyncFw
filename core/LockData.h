/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file LockData.h @brief The LockData class. */

#include <shared_mutex>

namespace AsyncFw {
namespace Mode {
/** @enum Lock LockData.h <AsyncFw/LockData> @brief Selects the locking discipline for a LockData carrier.
Mode::Lock::Mutex uses std::mutex (exclusive, no concurrent readers). Mode::Lock::Exclusive uses std::shared_mutex::lock. Mode::Lock::Shared uses std::shared_mutex::lock_shared. */
enum class Lock {
  Mutex,      ///< std::mutex, exclusive semantics. Cheaper than shared_mutex; no concurrent readers.
  Exclusive,  ///< std::shared_mutex exclusive (write) lock. Blocks all readers until released.
  Shared      ///< std::shared_mutex shared (read) lock. Multiple readers may hold it simultaneously.
};
}  // namespace Mode

/** @struct LockData LockData.h <AsyncFw/LockData> @brief A zero-copy RAII carrier that couples a synchronized resource view with its active lock guard.
@details The underlying mutex type and locking discipline are selected at compile time via the M template parameter: Mode::Lock::Mutex uses std::mutex with exclusive semantics, Mode::Lock::Exclusive uses std::shared_mutex::lock, and Mode::Lock::Shared uses std::shared_mutex::lock_shared. The lock is acquired in the constructor and released in the destructor, so the protected resource remains valid for the lifetime of the LockData instance.
@tparam T The resource type declaration, which must be either a reference or a pointer. @tparam M The locking discipline. Defaults to Mode::Lock::Mutex, which uses std::mutex with exclusive semantics.
@brief Example: @snippet snippet.dox LockData */
template <typename T, Mode::Lock M = Mode::Lock::Mutex>
struct LockData {
  using _T = std::conditional_t<M == Mode::Lock::Shared, const std::remove_reference_t<T>, std::remove_reference_t<T>>;
  using _M = std::conditional_t<M == Mode::Lock::Mutex, std::mutex, std::shared_mutex>;
  /** @brief Constructs the carrier by binding a raw pointer target and acquiring the lock.
  @details Acquires std::mutex::lock for Mode::Lock::Mutex, std::shared_mutex::lock for Mode::Lock::Exclusive, or std::shared_mutex::lock_shared for Mode::Lock::Shared. */
  LockData(_T *data, _M &mutex) : data_(data), mutex_(&mutex) {
    if constexpr (M == Mode::Lock::Shared) mutex_->lock_shared();
    else mutex_->lock();
  }
  /** @brief Constructs the carrier by taking the address of an lvalue reference. Delegates to the pointer overload. */
  LockData(_T &data, _M &mutex) : LockData(&data, mutex) {}
  /** @brief Move constructor that transfers the critical section ownership from an rvalue LockData state.
  @details The source is left without lock ownership and with a null data pointer. */
  LockData(LockData &&locked) : data_(locked.data_), mutex_(locked.mutex_) {
    locked.data_ = nullptr;
    locked.mutex_ = nullptr;
  }
  /** @brief Destructor that releases the acquired lock if this instance still retains its ownership.
  @details Mirrors the constructor: unlock for Mutex and Exclusive, unlock_shared for Shared. A moved-from instance holds no lock and does nothing on destruction. */
  ~LockData() {
    if (!mutex_) return;
    if constexpr (M == Mode::Lock::Shared) mutex_->unlock_shared();
    else mutex_->unlock();
  }
  /** @brief Member access operator returning a pointer to the protected resource.
  @details The pointee type is mutable for Mode::Lock::Mutex and Mode::Lock::Exclusive, and const for Mode::Lock::Shared. */
  _T *operator->() const { return data_; }
  /** @brief Dereference operator returning a reference to the protected resource.
  @details The reference type is mutable for Mode::Lock::Mutex and Mode::Lock::Exclusive, and const for Mode::Lock::Shared. */
  _T &operator*() const { return *data_; }

protected:
  _T *data_;   ///< Pointer to the protected inner resource.
  _M *mutex_;  ///< Pointer to the acquired lock (std::mutex or std::shared_mutex, per M); null after moving.
};
}  // namespace AsyncFw
