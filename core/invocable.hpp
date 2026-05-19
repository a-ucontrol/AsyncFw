/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once
namespace AsyncFw {
template <typename... Args>
struct Invocable {
  template <typename R>
  struct Abstract {
    virtual R operator()(Args...) = 0;
    virtual ~Abstract() = default;
  };
  template <typename F, typename R = std::invoke_result<F, Args...>::type>
  struct Function : public Invocable::Abstract<R> {
    R operator()(Args... args) override { return f_(std::forward<Args>(args)...); }
    Function(F &&f) : f_(std::move(f)) {}

  private:
    F f_;
  };
  template <typename M, typename O, typename R = std::invoke_result<M, O, Args...>::type>
  struct MemberFunction : public Invocable::Abstract<R> {
    R operator()(Args... args) override { return (o_->*m_)(std::forward<Args>(args)...); }
    MemberFunction(M m, O o) : m_(m), o_(o) {}

  private:
    M m_;
    O o_;
  };
};
}  // namespace AsyncFw
