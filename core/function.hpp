#pragma once

namespace AsyncFw {
template <typename... Args>
struct AbstractFunction {
  virtual void operator()(Args...) = 0;
  virtual ~AbstractFunction() = default;
};
template <typename... Args>
struct FunctionArgs {
  template <typename T>
  struct Function : public AbstractFunction<Args...> {
    void operator()(Args... args) override { f(args...); }
    Function(T &_f) : f(std::move(_f)) {}

  private:
    T f;
  };
};
}  // namespace AsyncFw
