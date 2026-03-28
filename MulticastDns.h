#pragma once

#include "core/FunctionConnector.h"
#include "instance.hpp"

namespace AsyncFw {
class AbstractThread;
/*! \brief The MulticastDns class.
 \brief Example: \snippet MulticastDns/main.cpp snippet */
class MulticastDns {
public:
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
  ~MulticastDns();

  //int sendDnsSd();
  int sendQuery(int timeout = 0);

  const std::vector<Host> hosts() const {
    std::lock_guard<std::mutex> lock(mutex);
    return hosts_;
  }

  void setServiceType(const std::string &_serviceType);
  std::string serviceType() { return serviceType_; };

  bool startService(const std::string &hostname, const std::string &llip, const std::string &misc, uint16_t port);
  void stopService(bool send_goodbye = true);
  bool serviceRunning();

  bool startQuerier(int timeout = 60);
  void stopQuerier();
  bool querierRunning();

  void append_(const Host &host);

  FunctionConnectorProtected<MulticastDns>::Connector<const Host &> hostAdded;
  FunctionConnectorProtected<MulticastDns>::Connector<const Host &> hostChanged;
  FunctionConnectorProtected<MulticastDns>::Connector<const Host &> hostRemoved;

private:
  static inline Instance<MulticastDns> instance_ {"HttpServer"};
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
};
}  // namespace AsyncFw
