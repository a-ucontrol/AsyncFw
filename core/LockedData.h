/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file LockedData.h @brief The LockedData class. */

#include <mutex>

namespace AsyncFw {
/** @struct LockedData LockedData.h <AsyncFw/LockedData> @brief A zero-copy RAII carrier that couples a synchronized resource view with its active lock guard.
  @details Enforces compile-time checks to ensure that the underlying type is strictly a reference or a pointer, effectively preventing accidental heavy memory allocations or copying during multi-threaded data sharing.
  @tparam T The resource type declaration, which must be either a reference or a pointer. */
template <typename T>
struct LockedData {
  static_assert(std::is_reference_v<T> || std::is_pointer_v<T>, "LockedData<T> error: T must be either a reference or a pointer to avoid copying!");
  using _T = std::remove_reference_t<T>;
  /** @brief Constructs the LockedData container by binding a raw pointer target directly. */
  LockedData(_T *data, std::mutex &mutex) : data_(data), mutex_(&mutex) { mutex_->lock(); }
  /** @brief Constructs the LockedData container by taking the address of an lvalue reference. */
  LockedData(_T &data, std::mutex &mutex) : LockedData(&data, mutex) {}
  /** @brief Move constructor that transfers the critical section ownership from an rvalue LockedData state. */
  LockedData(const LockedData &&locked) : data_(locked.data_), mutex_(locked.mutex_) { locked.mutex_ = nullptr; }
  /** @brief Destructor that safely releases the acquired mutex if this instance still retains its ownership. */
  ~LockedData() {
    if (mutex_) mutex_->unlock();
  }
  /** @brief Indirection operator that returns a pointer to the protected inner resource. */
  _T *operator->() { return data_; }
  /** @brief Indirection operator that returns a reference to the protected inner resource. */
  _T &operator*() { return *data_; }

protected:
  _T *data_;
  mutable std::mutex *mutex_;
};
}  // namespace AsyncFw
