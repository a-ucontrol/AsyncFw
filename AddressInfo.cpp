/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <ares.h>
#include <cstring>

#include "core/Thread.h"
#include "core/LogStream.h"
#include "AddressInfo.h"

using namespace AsyncFw;

struct AddressInfo::Private {
  Private(AddressInfo *_ai) : ai(_ai) {
    thread = AbstractThread::currentThread();
    memset(&options, 0, sizeof(options));
    options.sock_state_cb_data = this;
    options.sock_state_cb = [](void *data, int s, int read, int write) {
      lsTrace("change state fd {} read:{} write:{}", s, read, write);
      AbstractThread::PollEvents e = (read) ? AbstractThread::PollIn : AbstractThread::PollNo | (write) ? AbstractThread::PollOut : AbstractThread::PollNo;
      if (static_cast<Private *>(data)->events) {
        if (!e) {
          static_cast<Private *>(data)->thread->removePollDescriptor(s);
          const ares_fd_events_t _ae = {s, ARES_FD_EVENT_NONE};
          ares_process_fds(static_cast<Private *>(data)->channel, &_ae, 1, 0);
        } else if (!(static_cast<Private *>(data)->events & e))
          static_cast<Private *>(data)->events = e;
        static_cast<Private *>(data)->thread->modifyPollDescriptor(s, e);
        return;
      }
      lsTrace() << "append poll descriptor" << s;
      static_cast<Private *>(data)->events = e;
      static_cast<Private *>(data)->thread->appendPollTask(s, e, [fd = s, ch = static_cast<Private *>(data)->channel](int _e) {
        lsDebug("poll event: {}, fd: {}", _e, fd) << ((_e != AbstractThread::PollOut) ? fd : 0);
        const ares_fd_events_t _ae = {fd, (_e == AbstractThread::PollOut) ? ARES_FD_EVENT_WRITE : ARES_FD_EVENT_READ};
        ares_process_fds(ch, &_ae, 1, 0);
      });
    };
    int status = ares_init_options(&channel, &options, ARES_OPT_SOCK_STATE_CB);
    if (status != ARES_SUCCESS) { lsError("Ares options init: {}", ares_strerror(status)); }
    lsTrace();
  }

  ~Private() {
    ares_destroy(channel);
    lsTrace();
  }

  AbstractThread::PollEvents events = AbstractThread::PollNo;
  ares_channel channel;
  ares_options options;
  AbstractThread *thread;
  AddressInfo *ai;
  int timeout = 10000;
  int tid = -1;
};

AddressInfo::AddressInfo() {
  private_ = new Private(this);
  lsTrace();
}

AddressInfo::~AddressInfo() {
  delete private_;
  lsTrace();
}
void AddressInfo::resolve(const std::string &name, Family f) {
  if (private_->tid >= 0) {
    lsError() << "timer task already exists";
    return;
  }
  private_->tid = private_->thread->appendTimerTask(private_->timeout, [this]() { ares_cancel(private_->channel); });

  struct ares_addrinfo_hints hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = f;
  hints.ai_flags = ARES_AI_CANONNAME;
  //hints.ai_socktype = SOCK_STREAM;

  ares_getaddrinfo(
      private_->channel, name.c_str(), nullptr, &hints,
      [](void *data, int status, int, struct ares_addrinfo *result) {
        static_cast<Private *>(data)->thread->removeTimer(static_cast<Private *>(data)->tid);
        static_cast<Private *>(data)->tid = -1;
        std::vector<std::string> _r;
        if (!result) lsWarning("Result: {}", ares_strerror(status));
        else {
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
        }
        static_cast<Private *>(data)->ai->completed(status, _r);
      },
      private_);
}

void AddressInfo::setTimeout(int _timeout) { private_->timeout = _timeout; }

CoroutineAwait AddressInfo::coResolve(const std::string &name, Family f) {
  return AsyncFw::CoroutineAwait([this, name, f](AsyncFw::CoroutineHandle h) {
    completed(
        [h](int, const std::vector<std::string> &list) {
          h.promise().setData(list);
          h.resume();
        },
        AsyncFw::AbstractFunctionConnector::Connection::Queued);
    resolve(name, f);
  });
}
