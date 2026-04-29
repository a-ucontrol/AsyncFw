/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

namespace AsyncFw {
template <typename R, typename... Args>
struct AbstractFunction {
  virtual R operator()(Args...) = 0;
  virtual ~AbstractFunction() = default;
};

template <typename T, typename... Args>
struct Function : public AbstractFunction<typename std::invoke_result<T, Args...>::type, Args...> {
  typename std::invoke_result<T, Args...>::type operator()(Args... args) override { return f(args...); }
  Function(T &_f) : f(std::move(_f)) {}

private:
  T f;
};
}  // namespace AsyncFw
