/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file MulticastDns.h @brief The MulticastDns class. */

#include <vector>
#include <string>
#include <mutex>
#include "main/mdns_data_types.h"
#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <netinet/in.h>
#endif
#include "3rdparty/mdns/mdns.h"  // Подключаем типы mdns_string_t, mdns_record_t и т.д.
#include "../core/FunctionConnector.h"
#include "Instance.h"

namespace AsyncFw {
class AbstractThread;
/** @class MulticastDns MulticastDns.h <AsyncFw/MulticastDns> @brief The MulticastDns class.
@brief Example: @snippet MulticastDns/main.cpp snippet */
class MulticastDns {
public:
  /** @brief The Host struct. */
  struct Host {
    std::string name;
    std::string ipv4;
    std::string llipv4;
    std::string misc;
    uint16_t port = 0;
    bool operator==(const Host &h) const { return name == h.name && ipv4 == h.ipv4 && llipv4 == h.llipv4 && misc == h.misc && port == h.port; }
    bool operator!=(const Host &h) const { return !operator==(h); }
  };

  static inline MulticastDns *instance() { return instance_.value; }
  MulticastDns(const std::string &serviceType = {});
  virtual ~MulticastDns();

  //int sendDnsSd();
  int sendQuery(int timeout = 0);

  const std::vector<Host> hosts() const {
    std::lock_guard<std::mutex> lock(mutex);
    return hosts_;
  }

  void setServiceType(const std::string &_serviceType);
  std::string serviceType() { return serviceType_; };

  bool startService(const std::string &hostname, const std::string &misc, uint16_t port);
  void stopService(bool send_goodbye = true);
  bool serviceRunning() const;

  bool startQuerier(int timeout = 60);
  void stopQuerier();
  bool querierRunning() const;

  void append_(const Host &host);

  FunctionConnector<const Host &>::Protected<MulticastDns> hostAdded;
  FunctionConnector<const Host &>::Protected<MulticastDns> hostChanged;
  FunctionConnector<const Host &>::Protected<MulticastDns> hostRemoved;

private:
  static Instance<MulticastDns> instance_;
  void servicePollEvent(int fd);
  void querierPollEvent(int fd);
  void querierTimerEvent();
  int queryTimeout_;
  int qtid;
  std::vector<int> sfds;
  std::vector<int> qfds;
  std::vector<Host> hosts_;
  std::string serviceType_;
  int sendQuery(const std::vector<std::pair<std::string, std::string>> &list, int timeout = 0);
  void update();
  mutable std::mutex mutex;
  AbstractThread *thread_;

public:  //!!!
  // Переносим структуры данных из Си прямо в тело C++ класса
  service_data_t sd_;           // Переменная структуры из общего хедера
  querier_data_t qd_;           // Переменная структуры из общего хедера
  std::vector<Host> hostList_;  // Локальный список хостов текущего инстанса
};
}  // namespace AsyncFw
