/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <any>

namespace AsyncFw {
/*! \brief The AnyData class. */
struct AnyData {
  template <typename T>
  T data() const {
    return std::any_cast<T>(data_);
  }
  AnyData(const std::any &);
  AnyData();
  ~AnyData();
  std::any &data() const { return data_; }
  void setData(const std::any &data) const { data_ = data; };

protected:
  mutable std::any data_;
};
}  // namespace AsyncFw
