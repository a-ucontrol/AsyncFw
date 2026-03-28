/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <MulticastDns.h>
#include <MainThread.h>
#include <Timer.h>
#include <core/LogStream.h>

using namespace AsyncFw;

int main(int argc, char *argv[]) {
  MulticastDns _mdns {"AsyncFw_mdns_example_service"};

  _mdns.hostAdded([](const MulticastDns::Host &host) { lsInfoGreen() << "Added" << host.name << host.ipv4 << host.llipv4 << host.misc << host.port; });
  _mdns.hostChanged([](const MulticastDns::Host &host) { lsInfoMagenta() << "Changed" << host.name << host.ipv4 << host.llipv4 << host.misc << host.port; });
  _mdns.hostRemoved([](const MulticastDns::Host &host) { lsInfoRed() << "Removed" << host.name << host.ipv4 << host.llipv4 << host.misc << host.port; });

  _mdns.startService("AsyncFw_host", "169.254.0.1", "AsyncFw_misc_string", 18080);
  _mdns.startQuerier(1);

  Timer::single(100, [&_mdns]() { _mdns.stopService(); });
  Timer::single(200, [&_mdns]() { MainThread::exit(); });

  lsNotice() << "Start Applicaiton";
  int ret = MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
