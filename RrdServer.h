/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "core/FunctionConnector.h"

namespace AsyncFw {
class DataArraySocket;
class DataArrayTcpServer;
class Rrd;

class RrdServer {
public:
  RrdServer(DataArrayTcpServer *, const std::vector<Rrd *> &Rrd);
  virtual ~RrdServer();
  void quit();

protected:
  void transmit(const DataArraySocket *socket, uint64_t index, uint32_t size, uint32_t pi);
  DataArrayTcpServer *tcpServer;
  std::vector<Rrd *> rrd;
  FunctionConnectionGuard rf_;
};
}  // namespace AsyncFw
