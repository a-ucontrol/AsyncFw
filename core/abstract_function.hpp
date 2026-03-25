#pragma once

namespace AsyncFw {
template <typename... Args>
struct AbstractFunction {
  virtual void invoke(Args...) = 0;
  virtual ~AbstractFunction() = default;
};
}  // namespace AsyncFw
