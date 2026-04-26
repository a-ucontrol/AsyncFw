#include <regex>
#include "core/AbstractThread.h"
#include "core/LogStream.h"
#include "3rdparty/mdns/mdns.h"
#include "MulticastDns.h"

#ifdef EXTEND_MDNS_TRACE
  #define trace lsTrace
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

#undef MDNS_STRING_FORMAT
#define MDNS_STRING_FORMAT(s) std::string(s.str, s.length)

using namespace AsyncFw;

static std::vector<MulticastDns::Host> hostList;

extern "C" {
extern mdns_string_t ipv4_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in *addr, size_t addrlen);
extern mdns_string_t ipv6_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in6 *addr, size_t addrlen);
extern mdns_string_t ip_address_to_string(char *buffer, size_t capacity, const struct sockaddr *addr, size_t addrlen);
extern int send_dns_sd();
extern char addrbuffer[64];
extern char entrybuffer[256];
extern char namebuffer[256];
extern mdns_record_txt_t txtbuffer[128];
extern char mdns_misc[128];
extern int mdns_send_goodbye;

extern int start_mdns_service(const char *hostname, const char *service_name, int service_port, int *_sockets[], int *_num_sockets);
extern void stop_mdns_service();
extern void mdns_service_event(int fd);

extern int start_mdns_querier(int *_sockets[], int *_num_sockets);
extern void stop_mdns_querier();
extern void mdns_querier_event(int fd);
extern int send_mdns_query(mdns_query_t *query, size_t count);

// Callback handling parsing answers to queries sent
int query_callback(int, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry, uint16_t, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size, size_t name_offset, size_t, size_t record_offset, size_t record_length, void *user_data) {
  mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from, addrlen);
  const char *entrytype = (entry == MDNS_ENTRYTYPE_ANSWER) ? "answer" : ((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
#ifdef LS_NO_TRACE
  (void)fromaddrstr;
  (void)entrytype;
#endif
  mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));
  if (rtype == MDNS_RECORDTYPE_PTR) {
    if (MDNS_STRING_FORMAT(entrystr) == MulticastDns::instance()->serviceType()) {
      mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      std::string name = std::regex_replace(MDNS_STRING_FORMAT(namestr), std::regex("\\..*"), "");
      bool b = false;
      for (MulticastDns::Host &host : hostList) {
        if (host.name == name) {
          host.ipv4.clear();
          b = true;
          break;
        }
      }
      if (!b) { hostList.push_back({name, {}, {}, {}, 0}); }
      trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name;
    } else
      trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_SRV) {
    if (std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("^[^\\.]*\\."), "") == MulticastDns::instance()->serviceType()) {
      mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      uint16_t port = (ttl) ? srv.port : 0;
      std::string name = std::regex_replace(MDNS_STRING_FORMAT(srv.name), std::regex(".local."), "");
      for (MulticastDns::Host &host : hostList) {
        if (host.name == name) {
          host.port = port;
          if (!port) {
            host.ipv4.clear();
            host.llipv4.clear();
            host.misc.clear();
          }
          if (*(void **)user_data == 0) *(void **)user_data = &host;
          break;
        }
      }
      trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name << port;
    } else
      trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_A) {
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    mdns_string_t addrstr = ipv4_address_to_string(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    std::string address = MDNS_STRING_FORMAT(addrstr);
    std::string name = std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex(".local."), "");
    for (MulticastDns::Host &host : hostList) {
      if (host.name == name) {
        host.ipv4 = address;
        if (*(void **)user_data == 0) *(void **)user_data = &host;
        break;
      }
    }
    trace() << "A" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(addrstr);
  } else if (rtype == MDNS_RECORDTYPE_TXT) {
    if (std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("^[^\\.]*\\."), "") == MulticastDns::instance()->serviceType()) {
      size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer, sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
      for (size_t itxt = 0; itxt < parsed; ++itxt) {
        std::string key = MDNS_STRING_FORMAT(txtbuffer[itxt].key);
        if (key != "llip" && key != "misc") continue;
        std::string name = std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("[.].*"), "");
        std::string val = MDNS_STRING_FORMAT(txtbuffer[itxt].value);
        for (MulticastDns::Host &host : hostList) {
          if (host.name == name) {
            if (key == "llip") host.llipv4 = val;
            else if (key == "misc") { host.misc = val; }
            if (*(void **)user_data == 0) *(void **)user_data = &host;
            break;
          }
        }
        trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(txtbuffer[itxt].key) << "=" << MDNS_STRING_FORMAT(txtbuffer[itxt].value);
      }
    } else
      trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_AAAA) {
    trace() << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << "MDNS_RECORDTYPE_AAAA ignored";
  } else
    lsTrace() << "unknown RECORDTYPE:" << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << rtype << rclass << ttl << record_length;
  return 0;
}

void append_host(void *_host) {
  MulticastDns::Host host = *(static_cast<MulticastDns::Host *>(_host));
  MulticastDns::instance()->append_(host);
}
}

Instance<MulticastDns> MulticastDns::instance_ {"MulticastDns"};

MulticastDns::MulticastDns(const std::string &_serviceType) {
  if (!instance_.value) instance_.value = this;
  else { logEmergency("Only one MulticastDNS can exist"); }
  thread_ = AbstractThread::currentThread();
  setServiceType(_serviceType);
  lsTrace();
}

MulticastDns::~MulticastDns() {
  if (instance_.value == this) instance_.value = nullptr;
  stopService();
  stopQuerier();
  lsTrace();
}

int MulticastDns::sendQuery(int timeout) { return sendQuery({{"PTR", serviceType_}}, timeout); }

void MulticastDns::setServiceType(const std::string &_serviceType) {
  serviceType_ = _serviceType;
  if (!serviceType_.empty() && serviceType_.c_str()[serviceType_.size() - 1] != '.') serviceType_ += '.';
}

/*
int MulticastDns::sendDnsSd() {
  lsDebug("Start");
  int ret = send_dns_sd();
  return ret;
}
*/
int MulticastDns::sendQuery(const std::vector<std::pair<std::string, std::string>> &list, int timeout) {
  lsTrace("send query");

  hostList.clear();
  mdns_query_t query[16];
  int query_count = 0;
  for (const std::pair<std::string, std::string> &pair : list) {
    query[query_count].name = pair.second.c_str();
    if (pair.first == "PTR") query[query_count].type = MDNS_RECORDTYPE_PTR;
    else if (pair.first == "SRV")
      query[query_count].type = MDNS_RECORDTYPE_SRV;
    else if (pair.first == "A")
      query[query_count].type = MDNS_RECORDTYPE_A;
    else if (pair.first == "AAAA")
      query[query_count].type = MDNS_RECORDTYPE_AAAA;
    else if (pair.first == "TXT")
      query[query_count].type = MDNS_RECORDTYPE_TXT;
    else
      query[query_count].type = MDNS_RECORDTYPE_ANY;

    trace() << query[query_count].type << query[query_count].name << pair.first;

    query[query_count].length = strlen(query[query_count].name);
    if ((++query_count) == 16) {
      lsError() << "overflow";
      break;
    }
  }

  int ret = send_mdns_query(query, query_count);
  if (ret < 0) lsError() << "error send query:" << ret;

  if (!timeout && !queryTimeout_) return ret;

  thread_->modifyTimer(qtid, ((timeout) ? timeout : queryTimeout_) * 1000);

  return ret;
}

bool MulticastDns::serviceRunning() { return !sfds.empty(); }

bool MulticastDns::startService(const std::string &hostname, const std::string &misc, uint16_t port) {
  if (!sfds.empty()) return false;
  snprintf(mdns_misc, sizeof(mdns_misc), "%s", misc.c_str());
  lsDebug() << hostname << serviceType_ << mdns_misc << port;
  int *sockets;
  int num;
  int r = start_mdns_service(hostname.c_str(), serviceType_.c_str(), port, &sockets, &num);
  lsTrace() << r << num;
  if (r || !num) return false;
  sfds.resize(num);
  for (size_t i = 0; i != sfds.size(); ++i) {
    sfds[i] = sockets[i];
    thread_->appendPollTask(sfds[i], AbstractThread::PollIn, [this, fd = sfds[i]](AbstractThread::PollEvents) { servicePollEvent(fd); });
  }
  return true;
}

void MulticastDns::servicePollEvent(int fd) {
  mdns_service_event(fd);
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::stopService(bool send_goodbye) {
  if (sfds.empty()) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  mdns_send_goodbye = send_goodbye;
  for (size_t i = 0; i != sfds.size(); ++i) thread_->removePollDescriptor(sfds[i]);
  sfds.clear();
  stop_mdns_service();
}

bool MulticastDns::startQuerier(int timeout) {
  if (!qfds.empty()) return false;
  int *sockets;
  int num;
  int r = start_mdns_querier(&sockets, &num);
  lsTrace() << r << num;
  if (r || !num) return false;
  qfds.resize(num);
  for (size_t i = 0; i != qfds.size(); ++i) {
    qfds[i] = sockets[i];
    thread_->appendPollTask(qfds[i], AbstractThread::PollIn, [this, fd = qfds[i]](AbstractThread::PollEvents) { querierPollEvent(fd); });
  }
  qtid = thread_->appendTimerTask(0, [this]() { querierTimerEvent(); });
  queryTimeout_ = timeout;
  sendQuery();
  return true;
}

void MulticastDns::querierPollEvent(int fd) {
  mdns_querier_event(fd);
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::querierTimerEvent() {
  trace() << LogStream::Color::Magenta << "update";
  update();
  if (queryTimeout_) sendQuery();
  else { thread_->modifyTimer(qtid, 0); }
}

void MulticastDns::stopQuerier() {
  if (qfds.empty()) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  for (size_t i = 0; i != qfds.size(); ++i) thread_->removePollDescriptor(qfds[i]);
  thread_->removeTimer(qtid);
  qfds.clear();
  stop_mdns_querier();

  hostList.clear();
  update();
  lsDebug() << "host list size:" << hosts_.size();
}

bool MulticastDns::querierRunning() { return !qfds.empty(); }

void MulticastDns::append_(const Host &_host) {
  for (auto host = hosts_.begin(); host != hosts_.end();) {
    if (_host.name == host->name) {
      if (_host == *host) return;
      lsDebug() << (*host).name << (*host).ipv4 << (*host).llipv4 << (*host).port << (*host).misc << std::endl << _host.name << _host.ipv4 << _host.llipv4 << _host.port << _host.misc;
      hostChanged(_host);
      std::lock_guard<std::mutex> lock(mutex);
      *host = _host;
      return;
    }
    host++;
  }
  std::lock_guard<std::mutex> lock(mutex);
  hosts_.push_back(_host);
  hostAdded(_host);
}

void MulticastDns::update() {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto host = hosts_.begin(); host != hosts_.end();) {
    if (std::find(hostList.begin(), hostList.end(), *host) == hostList.end()) {
      Host h = *host;
      host = hosts_.erase(host);
      hostRemoved(h);
    } else
      host++;
  }
}
