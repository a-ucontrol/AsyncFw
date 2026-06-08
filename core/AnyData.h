/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file AnyData.h @brief The AnyData class. */

#include <any>

namespace AsyncFw {
/** @class AnyData AnyData.h <AsyncFw/AnyData> @brief A generic, type-safe data container block wrapper utilizing std::any.
@details Serves as a lightweight polymorphic data attachment layer. It allows high-level framework components (such as asynchronous sockets or compiler-generated coroutine promises) to store, transport, and retrieve arbitrary user context or state data without modifying underlying class hierarchies. */
struct AnyData {
  /** @brief Type-safely extracts and returns the stored data value. @tparam T The expected concrete type of the stored data payload. @return A copy of the managed data object of type T.
  @note Throws std::bad_any_cast if the requested type T does not match the actual stored type. */
  template <typename T>
  T data() const {
    return std::any_cast<T>(data_);
  }
  /** @brief Constructs an AnyData storage shell with an optional initial type-safe payload. */
  AnyData(const std::any & = {});
  /** @brief Virtual-safe destructor allowing polymorphic cleanup of stored elements. */
  ~AnyData();
  /** @brief Retrieves a mutable reference to the underlying global raw container block. */
  std::any &data() const;
  /** @brief Overwrites or assigns a new type-safe payload inside the storage box. */
  void setData(const std::any &data) const;
  /** @brief Checks if the container currently holds no data payload values. */
  bool empty() const;

protected:
  mutable std::any data_;  ///< Storage container for holding type-safe variables on the fly.
};
}  // namespace AsyncFw
