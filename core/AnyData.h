/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <any>

namespace AsyncFw {
/*! \class AnyData AnyData.h <AsyncFw/AnyData> \brief The AnyData class. */
struct AnyData {
  template <typename T>
  T data() const {
    return std::any_cast<T>(data_);
  }
  AnyData(const std::any & = {});
  ~AnyData();
  std::any &data() const;
  void setData(const std::any &data) const;
  bool hasValue() const;

protected:
  mutable std::any data_;
};
}  // namespace AsyncFw
