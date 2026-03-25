#pragma once

namespace AsyncFw {
template <typename... Args>
struct AbstractFunction {
  virtual void operator()(Args...) = 0;
  virtual ~AbstractFunction() = default;
};
}  // namespace AsyncFw
