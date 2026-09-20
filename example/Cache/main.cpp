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

class ClassWithCache {
public:
  ClassWithCache() { config.store({"127.0.0.1", 8000, 50}); }
  AsyncFw::Cache<Config> config {50, [this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    auto conf = config.acquire();
    conf->ipAddress = "192.168.1.50";
    conf->port = 8080;
    conf->maxConnections = 100;
    config.touch();
  }};
  ~ClassWithCache() { lsTrace(); }
};

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CacheExamplePool");
  AsyncFw::Thread *thread = AsyncFw::ThreadPool::instance()->createThread("CacheExampleThread");
  ClassWithCache *configCache;

  thread->invoke([&configCache]() { configCache = new ClassWithCache(); }, true);
  thread->finished.connect([configCache]() {
    lsNotice() << "Destroy configCache";
    delete configCache;
  });
  AsyncFw::Timer timer;
  timer.timeout.connect([configCache]() {
    auto conf = configCache->config.acquire<AsyncFw::Mode::Lock::Shared>();
    lsInfo() << "Config IP:" << conf->ipAddress << "Port:" << conf->port;
    if (conf->port == 8080) AsyncFw::MainThread::exit(0);
  });

  timer.start(10);

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Application" << ret;

  return ret;
}
