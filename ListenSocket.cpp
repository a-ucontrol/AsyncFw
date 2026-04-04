/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#ifndef _WIN32
  #include <arpa/inet.h>
  #define close_fd ::close
#else
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define close_fd ::closesocket
#endif

#include "core/AbstractSocket.h"
#include "core/LogStream.h"
#include "core/Thread.h"
#include "ListenSocket.h"

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

void ListenSocket::incomingEvent() {
  sockaddr_storage _a;
  socklen_t _l = sizeof _a;
  int _cd = accept(fd_, (struct sockaddr *)&_a, &_l);
  std::string _pa;
  if (_a.ss_family == AF_INET) {
    char _ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(&_a)->sin_addr, _ip, sizeof _ip);
    _pa = _ip;
  } else {
    char _ip[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6 *>(&_a)->sin6_addr, _ip, sizeof _ip);
    _pa = _ip;
  }
  if (_cd >= 0) {
    bool _accept = false;
    incoming(_cd, _pa, &_accept);
    if (!_accept) {
      lsDebug() << LogStream::Color::Red << "failed incoming connection" << _cd;
      close_fd(_cd);
      return;
    }
  }
  trace() << LogStream::Color::Red << _cd << LogStream::Color::Green << _pa;
}

ListenSocket::~ListenSocket() {
  lsTrace();
  if (state_ == Destroy) return;
  state_ = Destroy;
  thread_->removeSocket(this);
  if (fd_ == -1) return;
  thread_->removePollDescriptor(fd_);
}
