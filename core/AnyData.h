#pragma once

#include <any>

namespace AsyncFw {
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
