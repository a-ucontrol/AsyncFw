/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Coroutine>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

template <typename T, typename... Args>
struct CoroutineFunctionAwait {
  CoroutineFunctionAwait(T &&f, Args &&...a) : f_(std::move(f)), t_(std::tuple(std::forward<Args>(a)...)) {}
  void await_suspend(CoroutineHandle h) const noexcept {
    h_ = h;
    ThreadPool::async([this]() {
      typename std::invoke_result<T, Args...>::type _r = std::apply(f_, t_);
      h_.promise().setData(_r);
      h_.promise().resume_queued();
    });
  }
  bool await_ready() const noexcept { return false; }
  typename std::invoke_result<T, Args...>::type await_resume() const noexcept { return h_.promise().data<typename std::invoke_result<T, Args...>::type>(); }

private:
  mutable CoroutineHandle h_;
  std::tuple<Args...> t_;
  T f_;
};

template <typename T, typename... Args>
CoroutineFunctionAwait<T, Args...> coFunction(T &&f, Args &&...args) {
  return CoroutineFunctionAwait {std::forward<T>(f), std::forward<Args>(args)...};
}

template <typename O, typename M, typename... Args>
auto coFunction(O *o, M m, Args &&...args) {
  return CoroutineFunctionAwait {[o, m](Args... args) { return (o->*m)(args...); }, std::forward<Args>(args)...};
}

class Tst {
public:
  double tst(int v1, double v2) {
    std::chrono::milliseconds(10);
    return v1 + v2 + 10000.0;
  }
};

CoroutineTask task() {
  double i = co_await CoroutineFunctionAwait(
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return static_cast<double>(v1 + v2 + 11.5);
      },
      100.5, 10.5);

  double j = co_await coFunction(
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return static_cast<double>(v1 + v2 + 11.5);
      },
      100.5, 15.5);

  Tst _tst;
  double k = co_await coFunction(&_tst, &Tst::tst, 100.5, 10.5);

  lsNotice() << i << j << k << AsyncFw::Thread::currentThread()->name();
  MainThread::exit();
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineAwaitExamplePool");

  task();

  lsNotice() << "MainThread::exec()";

  return MainThread::exec();
}
