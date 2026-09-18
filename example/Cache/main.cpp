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

int main(int argc, char *argv[]) {
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CacheExamplePool");
  AsyncFw::Timer timer;

  AsyncFw::Cache<Config> configCache(50, [&configCache]() {
    logNotice() << "Update...";
    AsyncFw::ThreadPool::async([&configCache]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      auto conf = configCache.acquire();
      conf->ipAddress = "192.168.1.50";
      conf->port = 8080;
      conf->maxConnections = 100;
      configCache.touch();
    });
  });

  configCache.store({"127.0.0.1", 8000, 50});

  timer.timeout.connect([&configCache]() {
    auto conf = configCache.acquire();
    logInfo() << "Config IP:" << conf->ipAddress << "Port:" << conf->port;
    if (conf->port == 8080) AsyncFw::MainThread::exit(0);
  });

  timer.start(10);

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Application" << ret;

  return ret;
}
