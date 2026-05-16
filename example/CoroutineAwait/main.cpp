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

template <typename... Args>
struct Example {
  template <typename R>
  struct Await {
    template <typename T>
    Await(T f, Args... args) : f_(new Function<Args...>::Value(std::forward<T>(f))), tuple_(args...) {}
    Await() = default;
    Await(const Await &) = delete;
    Await &operator=(const Await &) = delete;
    virtual ~Await() { delete f_; }
    virtual void await_suspend(CoroutineHandle h) const noexcept {
      h_ = h;
      ThreadPool::async([this]() { return std::apply(*f_, tuple_); },
                        [this](int _r) {
                          h_.promise().setData(_r);
                          h_.resume();
                        });
    }
    virtual bool await_ready() const noexcept { return false; }
    virtual R await_resume() const noexcept { return h_.promise().data<R>(); }

  private:
    mutable CoroutineHandle h_;
    Function<Args...>::template Abstract<R> *f_ = nullptr;
    std::tuple<Args...> tuple_;
  };
};

CoroutineTask task() {
  int i = co_await Example<double, double>::Await<int>(
      [](double v1, double v2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return static_cast<int>(v1 + v2 + 11);
      },
      101.5, 10.5);
  lsNotice() << i;
  MainThread::exit();
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineAwaitExamplePool", 5);

  task();

  lsNotice() << "MainThread::exec()";

  return MainThread::exec();
}
