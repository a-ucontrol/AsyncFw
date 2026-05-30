/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  AsyncFw::AbstractThread *_mainThread = AsyncFw::AbstractThread::current();
  AsyncFw::Thread thread1("T1");
  AsyncFw::Thread thread2("T2");

  thread1.start();
  thread2.start();

  logDebug() << "Main id:" << _mainThread->id();
  logDebug() << "T1 id:" << thread1.id();
  logDebug() << "T2 id:" << thread2.id();

  thread1.invoke([&thread2, _mainThread]() {
    AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
    logInfo() << "run in thread" << ct->name() << ct->id();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    thread2.invoke([_mainThread]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      logInfo() << "run in thread" << ct->name() << ct->id();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      _mainThread->invoke([_mainThread]() {
        AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
        logInfo() << "run in thread" << ct->name() << ct->id();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        logInfo() << "exit application";
        AsyncFw::MainThread::exit(0);
      });
    });
  });

  logNotice() << "Start Application";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Application";
  return ret;
}
