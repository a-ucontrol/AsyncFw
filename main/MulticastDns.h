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
  /** @enum QuerierMode @brief Operation modes for the network query browser.
  @details Defines how the discovery queries are broadcasted, which local ports are utilized, and how remote nodes are expected to respond. */
  enum QuerierMode : uint8_t {
    /** @brief Standard mDNS multicast mode.
    @details Binds to the standard mDNS port (5353). Queries are sent via multicast, and responses from remote nodes are also broadcasted via multicast to the entire network. Allows full caching by all peers but increases network traffic. */
    Multicast = 0,
    /** @brief Hybrid unicast mode via the standard port.
    @details Binds to the standard mDNS port (5353). Queries are sent via multicast, but remote nodes are strictly requested to send their responses via Unicast directly to this node. This minimizes network noise while still allowing the socket to passively capture unsolicited background @c Announce and @c Goodbye events from peers. */
    Unicast5353 = 1,
    /** @brief Legacy Unicast mode via an ephemeral port.
    @details Binds to a random, OS-assigned ephemeral port (0). Queries are sent via multicast, and responses are returned directly via Unicast. Useful as a failover when port 5353 is locked by another system daemon, but cannot capture unsolicited background multicast announcements or goodbyes. */
    Unicast = 2
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
  void stopService(bool = true);
  /** @brief Checks if the mDNS local responder is running. */
  bool serviceRunning() const;

  /** @brief Starts the background cyclic network polling task for host discovery. @param seconds Seconds between cyclic search queries. @param unicast If true, queries from ephemeral ports to enforce Unicast responses and suppress network multicast storms. */
  bool startQuerier(int = 60, QuerierMode = Unicast5353);
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
