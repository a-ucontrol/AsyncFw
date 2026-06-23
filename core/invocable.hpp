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
  /*
  CRITICAL WARNING ON FORWARDING / MOVE SEMANTICS:
  - DO NOT UNDER ANY CIRCUMSTANCES insert std::forward<Args>(args)... or std::move() here.
  - This class is a passive, general-purpose invoker. It must strictly preserve the argument
    valuation types passed into it without performing any implicit rvalue casting.

  - WHY NOT std::forward?
    If this class is instantiated with an lvalue-reference signature (e.g., Invocable<void(T&)>),
    std::forward collapses into std::forward<T&>, returning a strict LVALUE reference. If the
    underlying callable f_ accepts parameters BY VALUE, this forces an implicit COPY, silently
    destroying multicast signal-slot performance.

  - WHY NOT std::move?
    If we forced std::move here, it would turn arguments into rvalues, causing the VERY FIRST
    subscriber in a Signal multicast loop to steal/empty the resources, leaving all subsequent
    subscribers with broken, moved-from objects.

  - CONCLUSION:
    Keep these operators completely clean (return f_(args...);).
    Any single-use dispatching optimization (like moving values out of an asynchronous
    event-queue or a coroutine tuple) MUST be implemented on the dispatcher side (e.g., inside
    Connection or Awaiter routing logic) by explicitly casting values before invoking the signal.
  */
  template <typename F>
  struct Function final : public Abstract {
    Function(F &&f) : f_(std::forward<F>(f)) {}
    R operator()(Args... args) override { return f_(args...); }

  protected:
    F f_;
  };
  template <typename M, typename O>
  struct MemberFunction final : public Abstract {
    MemberFunction(M m, O *o) : m_(m), o_(o) {}
    R operator()(Args... args) override { return (o_->*m_)(args...); }

  protected:
    M m_;
    O *o_;
  };
};
}  // namespace AsyncFw
