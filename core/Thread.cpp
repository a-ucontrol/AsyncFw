/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <signal.h>
#include <algorithm>
#include "core/AbstractSocket.h"
#include "core/LogStream.h"
#include "Thread.h"

#ifdef EXTEND_THREAD_TRACE
  #define trace LogStream(+LogStream::Debug | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

bool Thread::Compare::operator()(const AbstractSocket *_s1, const AbstractSocket *_s2) const {
  if (_s1->fd_ != _s2->fd_) return _s1->fd_ < _s2->fd_;
  return _s1 < _s2;
}

Thread *Thread::currentThread() { return static_cast<Thread *>(AbstractThread::currentThread()); }

Thread::Thread(const std::string &name) : AbstractThread(name) { trace(); }

Thread::~Thread() {
  destroing();
  warning_if(!sockets_.empty()) << "socket list not empty" << sockets_.size();
  if (AbstractThread::running()) {
    lsWarning() << "destroy running thread" << '(' + name() + ')';
    quit();
    waitFinished();
  }
  for (; !sockets_.empty();) {
    AbstractSocket *_s = sockets_.back();
    sockets_.pop_back();
    _s->state_ = AbstractSocket::Destroy;
    delete _s;
  }
}

void Thread::startedEvent() {
#ifndef _WIN32
  sigset_t _s;
  sigemptyset(&_s);
  sigaddset(&_s, SIGPIPE);
  sigprocmask(SIG_BLOCK, &_s, nullptr);  //SIGPIPE if close fd while tls handshake, AbstractTlsSocket::acceptEvent()
#endif
  AbstractThread::startedEvent();
  started();
}

void Thread::finishedEvent() {
  AbstractThread::finishedEvent();
  finished();
}

void Thread::appendSocket(AbstractSocket *_socket) {
  checkCurrentThread();
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  sockets_.insert(it, _socket);
  trace() << LogStream::Color::Green << _socket->fd_;
}

void Thread::removeSocket(AbstractSocket *_socket) {
  checkCurrentThread();
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(sockets_.begin(), sockets_.end(), _socket, Compare());
  if (it != sockets_.end() && (*it) == _socket) {
    sockets_.erase(it);
    return;
  }
  lsTrace() << LogStream::Color::DarkRed << "not found" << _socket->fd_;
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const Thread &t) {
  int _size;
  t.invokeMethod([&t, &_size]() { _size = t.sockets_.size(); }, true);
  return log << *static_cast<const AbstractThread *>(&t) << '-' << _size;
}
}  // namespace AsyncFw
