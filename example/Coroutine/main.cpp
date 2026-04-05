/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Coroutine>
#include <AsyncFw/Log>

AsyncFw::CoroutineTask task() {
  AsyncFw::CoroutineAwait await([](AsyncFw::CoroutineHandle h) {
    AsyncFw::ThreadPool::async(
        [h]() {
          AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
          logInfo() << "task: run in thread" << ct->name() << ct->id();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        },
        [h]() {
          AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
          logInfo() << "task: resume in thread" << ct->name() << ct->id();
          h.resume();
        });
  });
  co_await await;
  AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
  logNotice() << "task: resumed in thread" << ct->name() << ct->id();
}

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineExamplePool");

  task();

  auto coro_task([]() -> AsyncFw::CoroutineTask {
    AsyncFw::CoroutineAwait await([](AsyncFw::CoroutineHandle h) {
      AsyncFw::ThreadPool::async(
          [h]() {
            AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
            logInfo() << "coro_task: run in thread" << ct->name() << ct->id();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
          },
          [h]() {
            AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
            logInfo() << "coro_task: resume in thread" << ct->name() << ct->id();
            h.resume();
          });
    });
    co_await await;
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
    logNotice() << "coro_task: resumed in thread" << ct->name() << ct->id();
    AsyncFw::MainThread::exit(0);
  });

  coro_task();

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  return ret;
}
