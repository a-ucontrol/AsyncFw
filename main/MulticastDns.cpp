/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <random>
#include "core/AbstractThread.h"
#include "core/LogStream.h"
#include "Timer.h"
#include "MulticastDns.h"
#include "main/mdns-types.h"

#ifdef EXTEND_MDNS_TRACE
  #define trace lsTrace
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

#undef MDNS_STRING_FORMAT
#define MDNS_STRING_FORMAT(s) std::string(s.str, s.length)
#define MDNS_STRING_VIEW_FORMAT(s) std::string_view(s.str, s.length)

using namespace AsyncFw;

extern "C" {
int query_callback(int, const struct sockaddr *, size_t, mdns_entry_type_t, uint16_t, uint16_t, uint16_t, uint32_t, const void *, size_t, size_t, size_t, size_t, size_t, void *);
extern mdns_string_t ipv4_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in *addr, size_t addrlen);
extern mdns_string_t ipv6_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in6 *addr, size_t addrlen);
extern mdns_string_t ip_address_to_string(char *buffer, size_t capacity, const struct sockaddr *addr, size_t addrlen);
extern int start_mdns_service(const char *hostname, const char *service_name, int service_port, void *sd_void_ptr);
extern void stop_mdns_service(void *sd_void_ptr, int send_goodbye);
extern int mdns_service_event(int fd, void *sd_void_ptr);
extern int start_mdns_querier(void *qd_void_ptr);
extern void stop_mdns_querier(void *qd_void_ptr);
extern void mdns_querier_event(int fd, void *qd_void_ptr, void *ctx_void_ptr);
extern int send_mdns_query(void *qd_void_ptr, mdns_query_t *query, size_t count);

struct MulticastDns::Private {
  service_data_t sd_ = {};      // Low-level C-responder configuration buffers.
  querier_data_t qd_ = {};      // Low-level C-browser tracking buffers.
  std::vector<Host> hostList_;  // Internal temporary host storage for cache synchronization.
  std::string serviceType_;
};

struct Compare {
  bool operator()(const MulticastDns::Host &h, const std::string &n) const { return h.name.compare(n) < 0; }
};

static std::string hostName(const std::string &raw) {
  size_t _p = raw.find('.');
  if (_p != std::string::npos) { return raw.substr(0, _p); }
  return raw;
}

// Callback handling parsing answers to queries sent
int query_callback(int, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry, uint16_t, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size, size_t name_offset, size_t, size_t record_offset, size_t record_length, void *user_data) {
  QueryContext *ctx = static_cast<QueryContext *>(user_data);
  if (!ctx || !ctx->privData) return 0;
  char addrbuffer[64];
  char namebuffer[256];
  char entrybuffer[256];

  MulticastDns::Private *priv = static_cast<MulticastDns::Private *>(ctx->privData);
  auto &currentHostList = priv->hostList_;

  mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from, addrlen);

  const char *entrytype = (entry == MDNS_ENTRYTYPE_ANSWER) ? "answer" : ((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
#ifdef LS_NO_TRACE
  (void)fromaddrstr;
  (void)entrytype;
#endif
  mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));
  if (rtype == MDNS_RECORDTYPE_PTR) {
    if (MDNS_STRING_VIEW_FORMAT(entrystr) == priv->serviceType_) {
      mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      std::string name = hostName(MDNS_STRING_FORMAT(namestr));
      std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
      if (it != currentHostList.end() && (*it).name == name) (*it).ipv4.clear();
      else { currentHostList.insert(it, {name, {}, {}, {}, 0}); }
      trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name;
    } else trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_SRV) {
    if (MDNS_STRING_VIEW_FORMAT(entrystr).find(priv->serviceType_) != std::string::npos) {
      mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      uint16_t port = (ttl) ? srv.port : 0;
      std::string name = hostName(MDNS_STRING_FORMAT(srv.name));
      std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
      if (it != currentHostList.end() && (*it).name == name) {
        (*it).port = port;
        if (!port) {
          (*it).ipv4.clear();
          (*it).llipv4.clear();
          (*it).misc.clear();
        }
      }
      trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name << port;
    } else trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_A) {
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    mdns_string_t addrstr = ipv4_address_to_string(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    std::string address = MDNS_STRING_FORMAT(addrstr);
    std::string name = hostName(MDNS_STRING_FORMAT(entrystr));
    std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
    if (it != currentHostList.end() && (*it).name == name) {
      if ((ntohl(addr.sin_addr.s_addr) & 0xFFFF0000) == 0xA9FE0000) (*it).llipv4 = address;
      else (*it).ipv4 = address;
      lsTrace() << LogStream::Color::DarkGreen << name << (*it).ipv4 << (*it).llipv4 << (*it).misc;
      ctx->resultHost = &(*it);
    }
    trace() << "A" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(addrstr);
  } else if (rtype == MDNS_RECORDTYPE_TXT) {
    if (MDNS_STRING_VIEW_FORMAT(entrystr).find(priv->serviceType_) != std::string::npos) {
      mdns_record_txt_t txtbuffer[128];
      size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer, sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
      std::string name = hostName(MDNS_STRING_FORMAT(entrystr));
      for (size_t itxt = 0; itxt < parsed; ++itxt) {
        std::string key = MDNS_STRING_FORMAT(txtbuffer[itxt].key);
        if (key != "llip" && key != "misc") continue;
        std::string val = MDNS_STRING_FORMAT(txtbuffer[itxt].value);
        std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
        if (it != currentHostList.end() && (*it).name == name) {
          if (key == "llip") (*it).llipv4 = val;
          else if (key == "misc") { (*it).misc = val; }
        }
        trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(txtbuffer[itxt].key) << "=" << MDNS_STRING_FORMAT(txtbuffer[itxt].value);
      }
    } else trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_AAAA) {
    trace() << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << "MDNS_RECORDTYPE_AAAA ignored";
  } else lsTrace() << "unknown RECORDTYPE:" << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << rtype << rclass << ttl << record_length;
  return 0;
}
}

Instance<MulticastDns> MulticastDns::instance_ {"MulticastDns"};

MulticastDns::MulticastDns(const std::string &_serviceType) : private_(*new Private()) {
  thread_ = AbstractThread::current();
  setServiceType(_serviceType);
  lsTrace();
}

MulticastDns::~MulticastDns() {
  if (instance_.value == this) instance_.value = nullptr;
  stopService();
  stopQuerier();
  delete &private_;
  lsTrace();
}

int MulticastDns::sendQuery(int seconds) { return sendQuery({{"PTR", private_.serviceType_}}, seconds); }

void MulticastDns::setServiceType(const std::string &serviceType) {
  private_.serviceType_ = serviceType;
  if (!private_.serviceType_.empty() && private_.serviceType_.c_str()[private_.serviceType_.size() - 1] != '.') private_.serviceType_ += '.';
}

std::string MulticastDns::serviceType() { return private_.serviceType_; }

int MulticastDns::sendQuery(const std::vector<std::pair<std::string, std::string>> &list, int seconds) {
  lsTrace("send query");

  private_.hostList_.clear();
  mdns_query_t query[16];
  int query_count = 0;
  for (const std::pair<std::string, std::string> &pair : list) {
    query[query_count].name = pair.second.c_str();
    if (pair.first == "PTR") query[query_count].type = MDNS_RECORDTYPE_PTR;
    else if (pair.first == "SRV") query[query_count].type = MDNS_RECORDTYPE_SRV;
    else if (pair.first == "A") query[query_count].type = MDNS_RECORDTYPE_A;
    else if (pair.first == "AAAA") query[query_count].type = MDNS_RECORDTYPE_AAAA;
    else if (pair.first == "TXT") query[query_count].type = MDNS_RECORDTYPE_TXT;
    else query[query_count].type = MDNS_RECORDTYPE_ANY;

    trace() << query[query_count].type << query[query_count].name << pair.first;

    query[query_count].length = strlen(query[query_count].name);
    if ((++query_count) == 16) {
      lsError() << "overflow";
      break;
    }
  }

  int ret = send_mdns_query(&private_.qd_, query, query_count);
  if (ret < 0) lsError() << "error send query:" << ret;

  if (!seconds && !queryTimeout_) return ret;

  thread_->modifyTimer(qtid, ((seconds) ? seconds : queryTimeout_) * 1000);

  return ret;
}

bool MulticastDns::serviceRunning() const { return private_.sd_.num_sockets > 0; }

bool MulticastDns::startService(const std::string &hostname, const std::string &misc, uint16_t port) {
  if (private_.sd_.num_sockets > 0) return false;
  std::snprintf(private_.sd_.txt_misc_buffer, sizeof(private_.sd_.txt_misc_buffer), "%s", misc.c_str());
  lsDebug() << hostname << private_.serviceType_ << private_.sd_.txt_misc_buffer << port;
  int r = start_mdns_service(hostname.c_str(), private_.serviceType_.c_str(), port, &private_.sd_);
  lsTrace() << r << private_.sd_.num_sockets;
  if (r || !private_.sd_.num_sockets) return false;
  for (int i = 0; i != private_.sd_.num_sockets; ++i) {
    int fd = private_.sd_.sockets[i];
    thread_->appendPollTask(fd, AbstractThread::PollIn, [this, fd](AbstractThread::PollEvents e) {
      if (!(e & AbstractThread::PollIn)) {
        thread_->removePollDescriptor(fd);
        lsError() << "poll descriptor error:" << e;
        return;
      }
      thread_->modifyPollDescriptor(fd, AbstractThread::PollNo);  //Убираем fd из epoll
      uint16_t _val = ntohl(private_.sd_.service_address_llipv4.sin_addr.s_addr) & 0xFFFF;
      if (_val == 0) {
        static thread_local std::mt19937 generator(std::random_device {}());
        static thread_local std::uniform_int_distribution<int> distribution(0, 100);
        _val = 20 + distribution(generator);
      } else _val = 20 + (_val % 101);
      Timer::single(_val, [this, fd]() {
        thread_->modifyPollDescriptor(fd, AbstractThread::PollIn);  //Возвращаем fd в epoll
        servicePollEvent(fd);
      });
    });
  }
  return true;
}

void MulticastDns::servicePollEvent(int fd) {
  while (mdns_service_event(fd, &private_.sd_) > 0);
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::stopService(bool goodbye) {
  if (private_.sd_.num_sockets == 0) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  for (int i = 0; i != private_.sd_.num_sockets; ++i) { thread_->removePollDescriptor(private_.sd_.sockets[i]); }
  stop_mdns_service(&private_.sd_, goodbye);
  private_.sd_.num_sockets = 0;
}

bool MulticastDns::startQuerier(int seconds, QuerierMode mode) {
  if (private_.qd_.num_sockets > 0) return false;
  private_.qd_.mode = mode;
  int r = start_mdns_querier(&private_.qd_);
  lsTrace() << r << private_.qd_.num_sockets;
  if (r || !private_.qd_.num_sockets) return false;
  for (int i = 0; i != private_.qd_.num_sockets; ++i) {
    int fd = (private_.qd_.sockets)[i];
    thread_->appendPollTask(fd, AbstractThread::PollIn, [this, fd](AbstractThread::PollEvents) { querierPollEvent(fd); });
  }
  qtid = thread_->appendTimerTask(0, [this]() { querierTimerEvent(); });
  queryTimeout_ = seconds;
  sendQuery();
  return true;
}

void MulticastDns::querierPollEvent(int fd) {
  QueryContext ctx = {};
  ctx.privData = &private_;
  ctx.resultHost = nullptr;
  mdns_querier_event(fd, &private_.qd_, &ctx);
  if (ctx.resultHost) {
    auto *_host = static_cast<Host *>(ctx.resultHost);
    for (auto host = hosts_.begin(); host != hosts_.end();) {
      if (_host->name == host->name) {
        if (*_host == *host) return;
        lsDebug() << (*host).name << (*host).ipv4 << (*host).llipv4 << (*host).port << (*host).misc << std::endl << _host->name << _host->ipv4 << _host->llipv4 << _host->port << _host->misc;
        hostChanged(*_host);
        std::lock_guard<std::mutex> lock(mutex);
        *host = *_host;
        return;
      }
      host++;
    }
    std::lock_guard<std::mutex> lock(mutex);
    hosts_.push_back(*_host);
    hostAdded(*_host);
  }
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::querierTimerEvent() {
  trace() << LogStream::Color::Magenta << "update";
  update();
  if (queryTimeout_) sendQuery();
  else { thread_->modifyTimer(qtid, 0); }
}

void MulticastDns::stopQuerier() {
  if (private_.qd_.num_sockets == 0) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  for (int i = 0; i != private_.qd_.num_sockets; ++i) { thread_->removePollDescriptor((private_.qd_.sockets)[i]); }
  thread_->removeTimer(qtid);
  stop_mdns_querier(&private_.qd_);
  private_.qd_.num_sockets = 0;

  private_.hostList_.clear();
  update();
  lsDebug() << "host list size:" << hosts_.size();
}

bool MulticastDns::querierRunning() const { return private_.qd_.num_sockets > 0; }

void MulticastDns::update() {
  lsTrace() << LogStream::Color::Magenta << "update";
  std::lock_guard<std::mutex> lock(mutex);
  for (std::vector<MulticastDns::Host>::iterator host = hosts_.begin(); host != hosts_.end();) {
    std::vector<MulticastDns::Host>::iterator it = std::lower_bound(private_.hostList_.begin(), private_.hostList_.end(), host->name, Compare());
    if (it != private_.hostList_.end() && (*it).name == host->name) host++;
    else {
      hostRemoved(*host);
      host = hosts_.erase(host);
    }
  }
}
