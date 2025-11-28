#include <ares.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>

#include "core/Thread.h"
#include "core/LogStream.h"
#include "AddressInfo.h"

using namespace AsyncFw;

struct AddressInfo::Private {
  struct cbData {
    ares_channel c;
    AbstractThread *t;
    std::unordered_map<int, AbstractThread::PollEvents> m;
    AddressInfo *ai;
    int tid;
  };

  Private() {
    int status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS) {
      lsError("Ares library init: {}", ares_strerror(status));
      return;
    }

    memset(&options, 0, sizeof(options));
    options.sock_state_cb = [](void *data, int s, int read, int write) {
      lsTrace("change state fd {} read:{} write:{}", s, read, write);
      AbstractThread::PollEvents e = (read) ? AbstractThread::PollIn : AbstractThread::PollNo | (write) ? AbstractThread::PollOut : AbstractThread::PollNo;
      std::unordered_map<int, AbstractThread::PollEvents>::iterator it = static_cast<cbData *>(data)->m.find(s);
      if (it != static_cast<cbData *>(data)->m.end()) {
        if (!e) {
          static_cast<cbData *>(data)->t->removePollDescriptor(s);
          static_cast<cbData *>(data)->m.erase(it);
          const ares_fd_events_t _ae = {s, ARES_FD_EVENT_NONE};
          ares_process_fds(static_cast<cbData *>(data)->c, &_ae, 1, 0);
        } else if (!(it->second & e))
          static_cast<cbData *>(data)->t->modifyPollDescriptor(s, e);
        return;
      }

      lsTrace() << "append poll descriptor" << s;
      static_cast<cbData *>(data)->m.insert({s, e});

      static_cast<cbData *>(data)->t->appendPollTask(s, e, [fd = s, ch = static_cast<cbData *>(data)->c](int _e) {
        lsDebug("poll event: {}, fd: {}", _e, fd) << ((_e != AbstractThread::PollOut) ? fd : 0);
        const ares_fd_events_t _ae = {fd, (_e == AbstractThread::PollOut) ? ARES_FD_EVENT_WRITE : ARES_FD_EVENT_READ};
        ares_process_fds(ch, &_ae, 1, 0);
      });
    };
  }

  ~Private() {
    ares_destroy(channel);
    ares_library_cleanup();
    lsTrace();
  }

  ares_channel channel;
  ares_options options;
  AbstractThread *thread;
  int timeout = 10000;
};

AddressInfo::AddressInfo() {
  private_ = new Private();
  private_->thread = AbstractThread::currentThread();
  lsTrace();
}

AddressInfo::~AddressInfo() {
  delete private_;
  lsTrace();
}

void AddressInfo::resolve(const std::string &name, Family f) {
  Private::cbData *_data = new Private::cbData;
  _data->ai = this;

  _data->tid = private_->thread->appendTimerTask(private_->timeout, [_data]() { ares_cancel(_data->c); });

  private_->options.sock_state_cb_data = _data;

  int status = ares_init_options(&private_->channel, &private_->options, ARES_OPT_SOCK_STATE_CB);
  if (status != ARES_SUCCESS) {
    lsError("Ares options init: {}", ares_strerror(status));
    return;
  }

  _data->c = private_->channel;
  _data->t = private_->thread;

  struct ares_addrinfo_hints hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = f;
  hints.ai_flags = ARES_AI_CANONNAME;
  //hints.ai_socktype = SOCK_STREAM;

  ares_getaddrinfo(
      private_->channel, name.c_str(), nullptr, &hints,
      [](void *data, int status, int, struct ares_addrinfo *result) {
        static_cast<Private::cbData *>(data)->ai->private_->thread->removeTimer(static_cast<Private::cbData *>(data)->tid);
        static_cast<Private::cbData *>(data)->ai->private_->thread->invokeMethod([data]() { delete static_cast<Private::cbData *>(data); });
        if (result) {
          std::vector<std::string> _r;
          lsDebug("Result: {}, name: {}", ares_strerror(status), (result->name) ? result->name : "unknown");
          struct ares_addrinfo_node *node;
          for (node = result->nodes; node != nullptr; node = node->ai_next) {
            std::string addr_buf;
            const void *ptr = nullptr;
            if (node->ai_family == AF_INET) {
              const struct sockaddr_in *in_addr = (const struct sockaddr_in *)((void *)node->ai_addr);
              ptr = &in_addr->sin_addr;
              addr_buf.resize(INET_ADDRSTRLEN);
            } else if (node->ai_family == AF_INET6) {
              const struct sockaddr_in6 *in_addr = (const struct sockaddr_in6 *)((void *)node->ai_addr);
              ptr = &in_addr->sin6_addr;
              addr_buf.resize(INET6_ADDRSTRLEN);
            } else
              continue;

            ares_inet_ntop(node->ai_family, ptr, addr_buf.data(), addr_buf.size());
            addr_buf.resize(strlen(addr_buf.data()));
            _r.push_back(addr_buf);
          }
          ares_freeaddrinfo(result);
          static_cast<Private::cbData *>(data)->ai->completed(0, _r);
        } else {
          static_cast<Private::cbData *>(data)->ai->completed(-1, std::vector<std::string> {});
          lsWarning("Result: {}", ares_strerror(status));
        }
      },
      _data);
}

void AddressInfo::setTimeout(int _timeout) { private_->timeout = _timeout; }
