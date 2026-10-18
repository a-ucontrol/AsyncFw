/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
//
// Repro: queue starvation of the wake/exit eventfd.
//
// A chain task re-queues itself on every run, so the Main thread's task
// queue is never empty. exec() takes the snapshot, sees `tasks` non-empty,
// and `continue`s back to process_tasks() without ever entering poll().
// The exit eventfd therefore stays unread, MainThread::exit() is silently
// ignored, and only a direct quit() can break out.
//
// Expected behaviour:
//   - exit(0) at 500ms   -> exec() must return promptly  (fails today)
//   - fallback quit()    -> safety net, must always work
//
// Once exec() reads the wake/exit fd even while tasks are pending, the
// fallback timer will fire on an already-finished thread and be a no-op.
//
#include <AsyncFw/MainThread>
#include <AsyncFw/Thread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

class Chain {
public:
  void start() { enqueue(); }
  int count() const { return count_; }
  bool interrupted() const { return interrupted_; }

private:
  void enqueue() { AbstractThread::current()->invokeTask(new Invocable<void()>::Function([this]() { step(); })); }

  void step() {
    if (AbstractThread::current()->interruptRequested()) {
      interrupted_ = true;
      lsDebug() << "chain: interrupted after" << count_ << "iterations";
      return;
    }
    ++count_;
    if (count_ % 10000 == 0) lsInfo() << "chain:" << count_;
    enqueue();
  }

  int count_ = 0;
  bool interrupted_ = false;
};

int main(int argc, char *argv[]) {
  Thread *mainThread = Thread::current();
  Thread thread;
  thread.start();
  thread.invoke([mainThread](){
    Timer::single(500, [mainThread]() {
      lsDebug() << "calling MainThread::exit(0) at" << LogStream::currentTimeString({"%H:%M:%S", true});
      MainThread::quit();
    });
  });

  Chain chain;

  AbstractThread::current()->invoke([&chain]() { chain.start(); });

  logNotice() << "Start Application";
  int ret = MainThread::exec();
  logNotice() << "End Application" << ret;

  lsInfo() << "chain iterations:" << chain.count() << "interrupted:" << chain.interrupted();
  return ret;
}
//! [snippet]