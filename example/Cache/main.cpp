/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Timer>
#include <AsyncFw/Cache>
#include <AsyncFw/LogStream>

struct Config {
  std::string ipAddress;
  int port;
  int maxConnections;
};

int main(int, char **) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CacheExamplePool");
  AsyncFw::Cache<Config> configCache(50);
  AsyncFw::Timer timer;
  AsyncFw::ThreadPool::instance()->createThread("CacheExampleThread", new AsyncFw::Invocable<void(AsyncFw::ThreadPool::Thread *)>::Function([&configCache, &timer](AsyncFw::Thread *thread) {
    thread->started.connect([&configCache, &timer]() {
      configCache.update.connect([&configCache]() {
        lsNotice() << "update in:" << AsyncFw::Thread::current()->name();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        AsyncFw::LockData conf = configCache.acquire();
        conf->ipAddress = "192.168.1.50";
        conf->port = 8080;
        conf->maxConnections = 100;
        configCache.touch();
      });
      timer.start(10);
    });
  }));

  configCache.update.connect([&configCache]() { lsDebug() << "log only" << AsyncFw::Thread::current()->name(); });

  configCache.store({"127.0.0.1", 8000, 50});

  timer.timeout.connect([&configCache]() {
    AsyncFw::LockData conf = configCache.acquire<AsyncFw::Mode::Lock::Shared>();
    lsInfo() << "Config IP:" << conf->ipAddress << "Port:" << conf->port;
    if (conf->port == 8080) AsyncFw::MainThread::exit(0);
  });

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Application" << ret;

  return ret;
}
//! [snippet]
