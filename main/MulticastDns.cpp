/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <regex>
#include "core/AbstractThread.h"
#include "core/LogStream.h"
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

extern "C" {
extern mdns_string_t ipv4_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in *addr, size_t addrlen);
extern mdns_string_t ipv6_address_to_string(char *buffer, size_t capacity, const struct sockaddr_in6 *addr, size_t addrlen);
extern mdns_string_t ip_address_to_string(char *buffer, size_t capacity, const struct sockaddr *addr, size_t addrlen);
extern int send_dns_sd();

// Обновленные Си-функции: теперь они не хранят состояние, а принимают данные из C++
extern int start_mdns_service(const char *hostname, const char *service_name, int service_port, void *sd_void_ptr);
extern void stop_mdns_service(void *sd_void_ptr, int send_goodbye);
extern void mdns_service_event(int fd, void *sd_void_ptr);

extern int start_mdns_querier(void *qd_void_ptr);
extern void stop_mdns_querier(void *qd_void_ptr);
extern void mdns_querier_event(int fd, void *cpp_instance, void *qd_void_ptr);
extern int send_mdns_query(void *qd_void_ptr, mdns_query_t *query, size_t count);

struct Compare {
  bool operator()(const MulticastDns::Host &h, const std::string &n) const { return h.name.compare(n); }
};

// Callback handling parsing answers to queries sent
int query_callback(int, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry, uint16_t, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size, size_t name_offset, size_t, size_t record_offset, size_t record_length, void *user_data) {
  QueryContext *ctx = static_cast<QueryContext *>(user_data);
  if (!ctx || !ctx->instance) return 0;
  char addrbuffer[64];
  char namebuffer[256];
  char entrybuffer[256];
  MulticastDns *current_instance = static_cast<MulticastDns *>(ctx->instance);
  auto &currentHostList = current_instance->hostList_;  // Ссылка на локальный вектор объекта
  mdns_string_t fromaddrstr = ip_address_to_string(addrbuffer, sizeof(addrbuffer), from, addrlen);
  const char *entrytype = (entry == MDNS_ENTRYTYPE_ANSWER) ? "answer" : ((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
#ifdef LS_NO_TRACE
  (void)fromaddrstr;
  (void)entrytype;
#endif
  mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));
  if (rtype == MDNS_RECORDTYPE_PTR) {
    if (MDNS_STRING_FORMAT(entrystr) == current_instance->serviceType()) {
      mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      std::string name = std::regex_replace(MDNS_STRING_FORMAT(namestr), std::regex("\\..*"), "");
      std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
      if (it != currentHostList.end() && (*it).name == name) (*it).ipv4.clear();
      else { currentHostList.insert(it, {name, {}, {}, {}, 0}); }
      trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name;
    } else trace() << "PTR" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_SRV) {
    if (std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("^[^\\.]*\\."), "") == current_instance->serviceType()) {
      mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
      uint16_t port = (ttl) ? srv.port : 0;
      std::string name = std::regex_replace(MDNS_STRING_FORMAT(srv.name), std::regex(".local."), "");

      std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
      if (it != currentHostList.end() && (*it).name == name) {
        (*it).port = port;
        if (!port) {
          (*it).ipv4.clear();
          (*it).llipv4.clear();
          (*it).misc.clear();
        }
        if (ctx->resultHost == nullptr) ctx->resultHost = &(*it);
      }
      trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << name << port;
    } else trace() << "SRV" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_A) {
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    mdns_string_t addrstr = ipv4_address_to_string(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    std::string address = MDNS_STRING_FORMAT(addrstr);
    std::string name = std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex(".local."), "");
    std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
    if (it != currentHostList.end() && (*it).name == name) {
      (*it).ipv4 = address;
      if (ctx->resultHost == nullptr) ctx->resultHost = &(*it);
    }
    trace() << "A" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(addrstr);
  } else if (rtype == MDNS_RECORDTYPE_TXT) {
    if (std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("^[^\\.]*\\."), "") == current_instance->serviceType()) {
      mdns_record_txt_t txtbuffer[128];
      size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer, sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
      for (size_t itxt = 0; itxt < parsed; ++itxt) {
        std::string key = MDNS_STRING_FORMAT(txtbuffer[itxt].key);
        if (key != "llip" && key != "misc") continue;
        std::string name = std::regex_replace(MDNS_STRING_FORMAT(entrystr), std::regex("[.].*"), "");
        std::string val = MDNS_STRING_FORMAT(txtbuffer[itxt].value);
        std::vector<MulticastDns::Host>::iterator it = std::lower_bound(currentHostList.begin(), currentHostList.end(), name, Compare());
        if (it != currentHostList.end() && (*it).name == name) {
          if (key == "llip") (*it).llipv4 = val;
          else if (key == "misc") { (*it).misc = val; }
          if (ctx->resultHost == nullptr) ctx->resultHost = &(*it);
        }
        trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << MDNS_STRING_FORMAT(txtbuffer[itxt].key) << "=" << MDNS_STRING_FORMAT(txtbuffer[itxt].value);
      }
    } else trace() << "TXT" << std::endl << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr);
  } else if (rtype == MDNS_RECORDTYPE_AAAA) {
    trace() << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << "MDNS_RECORDTYPE_AAAA ignored";
  } else lsTrace() << "unknown RECORDTYPE:" << MDNS_STRING_FORMAT(fromaddrstr) << entrytype << MDNS_STRING_FORMAT(entrystr) << rtype << rclass << ttl << record_length;
  return 0;
}

//void append_host(void *_host) {
//  MulticastDns::Host host = *(static_cast<MulticastDns::Host *>(_host));
//  MulticastDns::instance()->append_(host);
//}
//}

void append_host(void *_host, void *cpp_instance) {
  if (!cpp_instance || !_host) return;

  // Восстанавливаем конкретный экземпляр C++ класса, который владеет этим квериером
  MulticastDns *current_instance = static_cast<MulticastDns *>(cpp_instance);
  MulticastDns::Host host = *(static_cast<MulticastDns::Host *>(_host));

  // Вызываем append_ строго для этого инстанса!
  current_instance->append_(host);
}
}

Instance<MulticastDns> MulticastDns::instance_ {"MulticastDns"};

MulticastDns::MulticastDns(const std::string &_serviceType) {
  thread_ = AbstractThread::current();
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

  hostList_.clear();
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

  int ret = send_mdns_query(&qd_, query, query_count);
  if (ret < 0) lsError() << "error send query:" << ret;

  if (!timeout && !queryTimeout_) return ret;

  thread_->modifyTimer(qtid, ((timeout) ? timeout : queryTimeout_) * 1000);

  return ret;
}

//bool MulticastDns::serviceRunning() const { return !sfds.empty(); } //!!!
bool MulticastDns::serviceRunning() const { return sd_.num_sockets > 0; }

bool MulticastDns::startService(const std::string &hostname, const std::string &misc, uint16_t port) {
  if (sd_.num_sockets > 0) return false;

  // Копируем misc в локальный буфер структуры средствами C++
  std::snprintf(sd_.txt_misc_buffer, sizeof(sd_.txt_misc_buffer), "%s", misc.c_str());

  lsDebug() << hostname << serviceType_ << sd_.txt_misc_buffer << port;

  // Вызываем функцию, где ровно 4 аргумента — регистры идеально совпадут!
  int r = start_mdns_service(hostname.c_str(), serviceType_.c_str(), port, &sd_);
  lsTrace() << r << sd_.num_sockets;
  if (r || !sd_.num_sockets) return false;

  for (int i = 0; i != sd_.num_sockets; ++i) {
    int fd = sd_.sockets[i];
    thread_->appendPollTask(fd, AbstractThread::PollIn, [this, fd](AbstractThread::PollEvents) { servicePollEvent(fd); });
  }
  return true;
}

void MulticastDns::servicePollEvent(int fd) {
  mdns_service_event(fd, &sd_);
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::stopService(bool send_goodbye) {
  if (sd_.num_sockets == 0) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  for (int i = 0; i != sd_.num_sockets; ++i) { thread_->removePollDescriptor(sd_.sockets[i]); }
  stop_mdns_service(&sd_, send_goodbye);
  sd_.num_sockets = 0;
}

bool MulticastDns::startQuerier(int timeout) {
  if (qd_.num_sockets > 0) return false;
  int r = start_mdns_querier(&qd_);
  lsTrace() << r << qd_.num_sockets;
  if (r || !qd_.num_sockets) return false;
  for (int i = 0; i != qd_.num_sockets; ++i) {
    int fd = (qd_.sockets)[i];
    thread_->appendPollTask(fd, AbstractThread::PollIn, [this, fd](AbstractThread::PollEvents) { querierPollEvent(fd); });
  }
  qtid = thread_->appendTimerTask(0, [this]() { querierTimerEvent(); });
  queryTimeout_ = timeout;
  sendQuery();
  return true;
}

void MulticastDns::querierPollEvent(int fd) {
  mdns_querier_event(fd, this, &qd_);
  trace() << LogStream::Color::Magenta << fd;
}

void MulticastDns::querierTimerEvent() {
  trace() << LogStream::Color::Magenta << "update";
  update();
  if (queryTimeout_) sendQuery();
  else { thread_->modifyTimer(qtid, 0); }
}

void MulticastDns::stopQuerier() {
  if (qd_.num_sockets == 0) {
    lsDebug() << LogStream::Color::Blue << "not running";
    return;
  }
  for (int i = 0; i != qd_.num_sockets; ++i) { thread_->removePollDescriptor((qd_.sockets)[i]); }
  thread_->removeTimer(qtid);
  stop_mdns_querier(&qd_);
  qd_.num_sockets = 0;

  hostList_.clear();
  update();
  lsDebug() << "host list size:" << hosts_.size();
}

bool MulticastDns::querierRunning() const { return qd_.num_sockets > 0; }
//bool MulticastDns::querierRunning() const { return !qfds.empty(); } //!!!

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
  for (std::vector<MulticastDns::Host>::iterator host = hosts_.begin(); host != hosts_.end();) {
    std::vector<MulticastDns::Host>::iterator it = std::lower_bound(hostList_.begin(), hostList_.end(), host->name, Compare());
    if (it != hostList_.end() && (*it).name == host->name) host++;
    else {
      hostRemoved(*host);
      host = hosts_.erase(host);
    }
  }
}
