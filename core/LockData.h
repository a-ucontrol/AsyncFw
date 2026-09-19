/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file LockData.h @brief The LockData class. */

#include <mutex>

namespace AsyncFw {
/** @struct LockData LockData.h <AsyncFw/LockData> @brief A zero-copy RAII carrier that couples a synchronized resource view with its active lock guard.
@details Enforces compile-time checks to ensure that the underlying type is strictly a reference or a pointer, effectively preventing accidental heavy memory allocations or copying during multi-threaded data sharing.
@tparam T The resource type declaration, which must be either a reference or a pointer. */
template <typename T>
struct LockData {
  static_assert(std::is_reference_v<T> || std::is_pointer_v<T>, "LockData<T> error: T must be either a reference or a pointer to avoid copying!");
  using _T = std::remove_reference_t<T>;
  /** @brief Constructs the LockData container by binding a raw pointer target and acquiring the mutex. */
  LockData(_T *data, std::mutex &mutex) : data_(data), mutex_(&mutex) { mutex_->lock(); }
  /** @brief Constructs the LockData container by taking the address of an lvalue reference. */
  LockData(_T &data, std::mutex &mutex) : LockData(&data, mutex) {}
  /** @brief Move constructor that transfers the critical section ownership from an rvalue LockData state. The source is left without lock ownership and with a null data pointer. */
  LockData(LockData &&locked) : data_(locked.data_), mutex_(locked.mutex_) {
    locked.data_ = nullptr;
    locked.mutex_ = nullptr;
  }
  /** @brief Destructor that safely releases the acquired mutex if this instance still retains its ownership. */
  ~LockData() {
    if (mutex_) mutex_->unlock();
  }
  /** @brief Indirection operator that returns a pointer to the protected inner resource. */
  _T *operator->() const { return data_; }
  /** @brief Indirection operator that returns a reference to the protected inner resource. */
  _T &operator*() const { return *data_; }

protected:
  _T *data_;           ///< Pointer to the protected inner resource.
  std::mutex *mutex_;  ///< Pointer to the synchronization primitive, cleared upon moving.
};
}  // namespace AsyncFw
