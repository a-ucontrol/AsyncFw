/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file MulticastDns.h @brief The MulticastDns class. */

#include "../core/FunctionConnector.h"
#include "Instance.h"

namespace AsyncFw {
class AbstractThread;
/** @class MulticastDns MulticastDns.h <AsyncFw/MulticastDns> @brief Asynchronous mDNS/DNS-SD service responder and discovery browser.
@details Multiple instances can run independently with different service types. */
class MulticastDns {
public:
  struct Private;
  /** @brief Represents a discovered network node or service instance. */
  struct Host {
    std::string name;    ///< Host or service name (e.g., "printer").
    std::string ipv4;    ///< Primary IPv4 address string.
    std::string llipv4;  ///< Link-Local IPv4 address (169.254.x.x).
    std::string misc;    ///< Metadata string from the mDNS TXT record.
    uint16_t port = 0;   ///< Target network port of the active service.
    bool operator==(const Host &h) const { return name == h.name && ipv4 == h.ipv4 && llipv4 == h.llipv4 && misc == h.misc && port == h.port; }
    bool operator!=(const Host &h) const { return !operator==(h); }
  };

  /** @brief Returns the global default instance pointer. */
  static inline MulticastDns *instance() { return instance_.value; }
  /** @brief Initializes the mDNS node with a specific target service type (e.g., "_http._tcp"). */
  MulticastDns(const std::string & = {});
  virtual ~MulticastDns();

  /** @brief Broadcasts an immediate multicast query to discover hosts. */
  int sendQuery(int = 0);

  /** @brief Thread-safe getter returning a snapshot of all discovered network hosts. */
  const std::vector<Host> hosts() const {
    std::lock_guard<std::mutex> lock(mutex);
    return hosts_;
  }

  /** @brief Sets the target mDNS/DNS-SD service descriptor pattern for the browser. */
  void setServiceType(const std::string &);
  /** @brief Returns the current tracked service classification. */
  std::string serviceType();

  /** @brief Registers and publishes a new local service to the network loop. */
  bool startService(const std::string &, const std::string &, uint16_t);
  /** @brief Unregisters the published service and optionally sends a goodbye notify pack. */
  void stopService(bool send_goodbye = true);
  /** @brief Checks if the mDNS local responder is running. */
  bool serviceRunning() const;

  /** @brief Starts the background cyclic network polling task for host discovery. */
  bool startQuerier(int = 60);
  /** @brief Terminates the active host search routine. */
  void stopQuerier();
  /** @brief Checks if the network query browser is actively polling. */
  bool querierRunning() const;

  FunctionConnector<const Host &>::Protected<MulticastDns> hostAdded;    ///< Triggered when a new device is found.
  FunctionConnector<const Host &>::Protected<MulticastDns> hostChanged;  ///< Triggered when a device alters parameters.
  FunctionConnector<const Host &>::Protected<MulticastDns> hostRemoved;  ///< Triggered when a device leaves or times out.

private:
  static Instance<MulticastDns> instance_;
  void servicePollEvent(int);
  void querierPollEvent(int);
  void querierTimerEvent();
  int queryTimeout_;
  int qtid;
  std::vector<Host> hosts_;
  int sendQuery(const std::vector<std::pair<std::string, std::string>> &, int = 0);
  void update();
  mutable std::mutex mutex;
  AbstractThread *thread_;

public:
  Private &private_;
};
}  // namespace AsyncFw
