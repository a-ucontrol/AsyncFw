/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

namespace AsyncFw {
template <typename... Args>
struct Function {
  template <typename R>
  struct Abstract {
    virtual R operator()(Args...) = 0;
    virtual ~Abstract() = default;
  };
  template <typename T>
  struct Value : public Function::Abstract<typename std::invoke_result<T, Args...>::type> {
    typename std::invoke_result<T, Args...>::type operator()(Args... args) override { return f(std::forward<Args>(args)...); }
    Value(T &&_f) : f(std::move(_f)) {}

  private:
    T f;
  };
};
}  // namespace AsyncFw
