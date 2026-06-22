/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once
namespace AsyncFw {
template <typename T>
struct Invocable;
template <typename R, typename... Args>
struct Invocable<R(Args...)> {
  struct Abstract {
    virtual R operator()(Args...) = 0;
    virtual ~Abstract() = default;
  };
  /* NOTE ON REFERENCE COLLAPSING & DEVIRTUALIZATION:
  - With value signatures (void(T)), std::forward triggers move semantics.
  - With reference signatures (void(T&)), it preserves lvalues, causing a COPY if the target callable accepts parameters by value.
  - The final keyword on implementations enables devirtualization optimizations. Override custom dispatchers if you need strict move/copy control. */
  template <typename F>
  struct Function final : public Abstract {
    Function(F &&f) : f_(std::forward<F>(f)) {}
    R operator()(Args... args) override { return f_(std::forward<Args>(args)...); }

  protected:
    F f_;
  };
  template <typename M, typename O>
  struct MemberFunction final : public Abstract {
    MemberFunction(M m, O *o) : m_(m), o_(o) {}
    R operator()(Args... args) override { return (o_->*m_)(std::forward<Args>(args)...); }

  protected:
    M m_;
    O *o_;
  };
};
}  // namespace AsyncFw
