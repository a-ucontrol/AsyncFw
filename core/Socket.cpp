#include <fcntl.h>

#include <cstring>

#include "Thread.h"
#include "LogStream.h"

#ifndef _WIN32
  #include <sys/socket.h>
  #include <sys/ioctl.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <linux/sockios.h>

  #define close_fd         ::close
  #define CONNECT_PROGRESS EINPROGRESS
  #define setsockopt_ptr
#else
  #include <winsock2.h>
  #include <ws2tcpip.h>

struct start_wsa {
  start_wsa() {
    WORD versionWanted = MAKEWORD(1, 1);
    WSADATA wsaData;
    (void)WSAStartup(versionWanted, &wsaData);
  }
  ~start_wsa() { WSACleanup(); }
} start_wsa;

  #define close_fd         ::closesocket
  #define CONNECT_PROGRESS 0
  #define setsockopt_ptr   reinterpret_cast<const char *>
#endif

#include "Socket.h"

#define SOCKET_CONNECTION_QUEUED 16

//#define EXTEND_SOCKET_TRACE

#ifdef EXTEND_SOCKET_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

using namespace AsyncFw;

struct AbstractSocket::Private {
  sockaddr_storage la_;
  sockaddr_storage pa_;
  DataArray rda_;
  DataArray wda_;
  int tid_ = -1;
  int errorCode_;
  std::string errorString_;
  int w_        = 0;
  int type_     = SOCK_STREAM;
  int protocol_ = IPPROTO_TCP;
};

#undef uC_THREAD
#define uC_THREAD this->thread()

AbstractSocket::AbstractSocket(SocketThread *_thread) {
  private_                = new Private;
  private_->la_.ss_family = AF_INET;
  private_->pa_.ss_family = AF_INET;
  thread_                 = (_thread) ? _thread : static_cast<SocketThread *>(AbstractThread::currentThread());
  thread_->appendSocket(this);
  trace();
}

AbstractSocket::AbstractSocket(int _family, int _type, int _protocol, SocketThread *_thread) : AbstractSocket(_thread) {
  private_->la_.ss_family = _family;
  private_->pa_.ss_family = _family;
  private_->type_         = _type;
  private_->protocol_     = _protocol;
}

AbstractSocket::~AbstractSocket() {
  if (thread_) thread_->removeSocket(this);
  if (fd_) close_fd(fd_);
  delete private_;
  trace();
}

bool AbstractSocket::listen(const std::string &_address, uint16_t _port) {
  int _fd = socket(private_->la_.ss_family, private_->type_, private_->protocol_);
  if (_fd < 0) {
    ucError() << "socket descriptor error" << _fd << errno;
    return false;
  }
  int _val = 1;
  if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, setsockopt_ptr(&_val), sizeof _val) < 0) ucError("set SO_REUSEADDR");
  //if (setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &_val, sizeof _val) < 0) ucError("set SO_REUSEPORT");

  reinterpret_cast<sockaddr_in *>(&private_->la_)->sin_port        = htons(_port);
  reinterpret_cast<sockaddr_in *>(&private_->la_)->sin_addr.s_addr = inet_addr(_address.c_str());

  if (::bind(_fd, reinterpret_cast<struct sockaddr *>(&private_->la_), sizeof(private_->la_)) || ::listen(_fd, SOCKET_CONNECTION_QUEUED)) {
    close_fd(_fd);
    ucError() << "listen error :" << _port;
    return false;
  }

  state_ = State::Listening;

  thread_->invokeMethod([this, _fd]() { changeDescriptor(_fd); }, true);
  thread_->appendPollTask(_fd, AbstractThread::PollIn, [this](AbstractThread::PollEvents _e) { pollEvent(_e); });

  stateEvent();

  return true;
}
/*
void AbstractSocket::timerEvent() {
  removeTimer();
  ucWarning("not implemented, timer removed");
}
*/
void AbstractSocket::errorEvent() { ucWarning("not implemented"); }

void AbstractSocket::acceptEvent() {
  if (state_ != Connected) {
    ucError() << LogStream::Color::Red << "not connected state";
    return;
  }
  state_ = Active;
  stateEvent();
}

void AbstractSocket::changeDescriptor(int _fd) {
  checkCurrentThread();
  std::vector<AbstractSocket *>::iterator it = std::lower_bound(thread_->sockets_.begin(), thread_->sockets_.end(), this, SocketThread::Compare());
  if (it != thread_->sockets_.end()) {
    thread_->sockets_.erase(it);
    fd_ = _fd;
    it  = std::lower_bound(thread_->sockets_.begin(), thread_->sockets_.end(), this, SocketThread::Compare());
    thread_->sockets_.insert(it, this);
  }
}

int AbstractSocket::read_available_fd() const {
#ifdef _WIN32
  u_long r;
  if (ioctlsocket(fd_, FIONREAD, &r) < 0) return -1;
#else
  int r;
  if (ioctl(fd_, SIOCINQ, &r) < 0) return -1;
#endif
  if (r > 0) return rs_ = r;
  if (r == 0) rs_ = -1;
  return r;
}

void AbstractSocket::setDescriptor(int _fd) {
  checkCurrentThread();
#ifndef _WIN32
  const int _f = fcntl(_fd, F_GETFL, 0);
  fcntl(_fd, F_SETFL, _f | O_NONBLOCK);
#else
  u_long _nb = 1;
  ioctlsocket(_fd, FIONBIO, &_nb);
#endif
  socklen_t _l;
  /*
  int _val = 1;
  if (setsockopt(_fd, SOL_TCP, TCP_NODELAY, &_val, sizeof _val) < 0) ucError("set TCP_NODELAY");
  _val = 8192;  //!!! need test
  if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_val, sizeof _val) < 0) ucError("set SO_SNDBUF");
  _val = 16384;
  if (setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &_val, sizeof _val) < 0) ucError("set SO_RCVBUF");
  _l = sizeof _val;
  if (getsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_val, &_l) < 0) ucError("SO_SNDBUF");
  else { ucDebug() << "SO_SNDBUF" << LogStream::Color::Red << _val; }
  if (getsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &_val, &_l) < 0) ucError("SO_RCVBUF");
  else { ucDebug() << "SO_RCVBUF" << LogStream::Color::Red << _val; }
*/
  _l = sizeof(private_->la_);
  if (getsockname(_fd, reinterpret_cast<struct sockaddr *>(&private_->la_), &_l) < 0) logError() << "Server: error socket address";
  _l = sizeof(private_->pa_);
  if (getpeername(_fd, reinterpret_cast<struct sockaddr *>(&private_->pa_), &_l) < 0) logError() << "Server: error peer address";

  ucTrace() << _fd << "local:" << LogStream::Color::Green << address() + ':' + std::to_string(port()) << "peer:" << LogStream::Color::Green << peerAddress() + ':' + std::to_string(peerPort());

  changeDescriptor(_fd);
  state_ = State::Connected;
  thread_->appendPollTask(fd_, AbstractThread::PollIn, [this](AbstractThread::PollEvents _e) { pollEvent(_e); });
  acceptEvent();
}

bool AbstractSocket::connect(const std::string &_address, uint16_t _port) {
  int _fd = socket(private_->pa_.ss_family, private_->type_, private_->protocol_);
  if (_fd < 0) {
    ucError() << "socket descriptor error";
    return false;
  }
#ifndef _WIN32
  const int _f = fcntl(_fd, F_GETFL, 0);
  fcntl(_fd, F_SETFL, _f | O_NONBLOCK);
#else
  u_long _nb = 1;
  ioctlsocket(_fd, FIONBIO, &_nb);
#endif
  socklen_t _l;
  /*
  int _val = 1;
  if (setsockopt(_fd, SOL_TCP, TCP_NODELAY, &_val, sizeof _val) < 0) ucError("set TCP_NODELAY");
  _val = 8192;  //!!! need test
  if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_val, sizeof _val) < 0) ucError("set SO_SNDBUF");
  _val = 16384;
  if (setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &_val, sizeof _val) < 0) ucError("set SO_RCVBUF");
  _l = sizeof _val;
  if (getsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &_val, &_l) < 0) ucError("SO_SNDBUF");
  else { ucDebug() << "SO_SNDBUF" << LogStream::Color::Red << _val; }
  if (getsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &_val, &_l) < 0) ucError("SO_RCVBUF");
  else { ucDebug() << "SO_RCVBUF" << LogStream::Color::Red << _val; }
*/
  reinterpret_cast<sockaddr_in *>(&private_->pa_)->sin_port        = htons(_port);
  reinterpret_cast<sockaddr_in *>(&private_->pa_)->sin_addr.s_addr = inet_addr(_address.c_str());

  if (::connect(_fd, reinterpret_cast<struct sockaddr *>(&private_->pa_), sizeof(private_->pa_)) == -1) {
    if (errno != CONNECT_PROGRESS) {
      close_fd(_fd);
      ucError() << "connect error" << _address + ':' + std::to_string(_port) << _fd << errno;
      return false;
    }
  }

  _l = sizeof(private_->la_);
  if (getsockname(_fd, reinterpret_cast<struct sockaddr *>(&private_->la_), &_l) < 0) logError() << "Client: error socket address";

  ucTrace() << _fd << "local:" << LogStream::Color::Green << address() + ':' + std::to_string(port()) << "peer:" << LogStream::Color::Green << peerAddress() + ':' + std::to_string(peerPort());

  thread_->invokeMethod([this, _fd]() { changeDescriptor(_fd); }, true);
  state_ = State::Connecting;
  thread_->appendPollTask(_fd, AbstractThread::PollOut, [this](AbstractThread::PollEvents _e) { pollEvent(_e); });

  return true;
}

void AbstractSocket::disconnect() {
  if (state_ == State::Unconnected) {
    ucError("not connected");
    return;
  }
  if (state_ == State::Closing) {
    ucError("already closing");
    return;
  }
  state_ = Closing;
  stateEvent();
  trace() << LogStream::Color::Red << fd_;
  if (private_->wda_.empty()) close();
}

int AbstractSocket::read(uint8_t *_p, int _s) {
  warning_if(_s <= 0) << LogStream::Color::Red << "size for read is null";
  if (!private_->rda_.empty()) {
    bool b = _s <= static_cast<int>(private_->rda_.size());
    memcpy(_p, private_->rda_.data(), (b) ? _s : private_->rda_.size());
    if (b) {
      private_->rda_.erase(private_->rda_.begin(), private_->rda_.begin() + _s);
      return _s;
    }
  }
  int r = read_fd(_p + private_->rda_.size(), _s - private_->rda_.size());
  if (r > 0) rs_ -= r;
  private_->rda_.clear();
  return r;
}

DataArray AbstractSocket::read(int _s) {
  DataArray _da;
  _da.resize((rs_ < _s) ? rs_ : _s);
  if (read(_da.data(), _da.size()) != static_cast<int>(_da.size())) return {};
  return _da;
}

int AbstractSocket::write(const uint8_t *_p, int _s) {
  warning_if(_s <= 0) << LogStream::Color::Red << "size for write is null";
  if (!private_->wda_.empty()) {
    private_->wda_.insert(private_->wda_.end(), _p, _p + _s);
    return _s;
  }
  if (!private_->w_) thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn | AbstractThread::PollOut);
  int r = write_fd(_p, _s);
  if (r > 0) {
    if (r < _s) private_->wda_.insert(private_->wda_.end(), _p + r, _p + _s);
    private_->w_ = 2;
    return _s;
  }
  return r;
}

int AbstractSocket::write(const DataArray &_da) { return write(_da.data(), _da.size()); }

void AbstractSocket::close() {
  if (!fd_ || !thread_) return;
  thread_->removePollDescriptor(fd_);
  close_fd(fd_);
  ucTrace() << LogStream::Color::Red << fd_;
  changeDescriptor(0);
  state_ = State::Unconnected;
  rs_    = 0;
  private_->rda_.clear();
  private_->wda_.clear();
  stateEvent();
}

void AbstractSocket::setErrorCode(int code) { private_->errorCode_ = code; }

int AbstractSocket::errorCode() { return private_->errorCode_; }

void AbstractSocket::setErrorString(const std::string &error) { private_->errorString_ = error; }

std::string AbstractSocket::errorString() const { return private_->errorString_; }

int AbstractSocket::pendingRead() const {
  if (rs_ > 0) return rs_;
  int r = read_available_fd();
  if (r < 0) return 0;
  return (rs_ = r) + private_->rda_.size();
}

int AbstractSocket::pendingWrite() const {
  /*int _s;
  int r = ioctl(fd_, SIOCOUTQ, &_s);
  if (r < 0) return r;
  return _s + private_->wda_.size();*/

  return private_->wda_.size();
}

int AbstractSocket::descriptorWriteSize() {
#ifndef _WIN32
  int _s;
#else
  char _s;
#endif
  socklen_t _l = sizeof(_s);
  int r        = getsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &_s, &_l);
  if (r == 0) return _s;
  return r;
}

std::string AbstractSocket::address() const {
  if (private_->la_.ss_family == AF_INET) {
    char _ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(&private_->la_)->sin_addr, _ip, sizeof _ip);
    return _ip;
  }
  char _ip[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in6 *>(&private_->la_)->sin6_addr, _ip, sizeof _ip);
  return _ip;
}

uint16_t AbstractSocket::port() const {
  if (private_->la_.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&private_->la_)->sin_port);
  return ntohs(((struct sockaddr_in6 *)&private_->la_)->sin6_port);
}

std::string AbstractSocket::peerAddress() const {
  if (private_->pa_.ss_family == AF_INET) {
    char _ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(&private_->pa_)->sin_addr, _ip, sizeof _ip);
    return _ip;
  }
  char _ip[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in6 *>(&private_->pa_)->sin6_addr, _ip, sizeof _ip);
  return _ip;
}

uint16_t AbstractSocket::peerPort() const {
  if (private_->pa_.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&private_->pa_)->sin_port);
  return ntohs(((struct sockaddr_in6 *)&private_->pa_)->sin6_port);
}

int AbstractSocket::read_fd(void *_p, int _s) {
#ifndef _WIN32
  return ::read(fd_, _p, _s);
#else
  return ::recv(fd_, static_cast<char *>(_p), _s, 0);
#endif
}

int AbstractSocket::write_fd(const void *_p, int _s) {
#ifndef _WIN32
  return ::write(fd_, _p, _s);
#else
  return ::sendto(fd_, static_cast<const char *>(_p), _s, 0, reinterpret_cast<struct sockaddr *>(&private_->la_), sizeof(private_->la_));
#endif
}

void AbstractSocket::destroy() {
  close();
  if (!thread_) {
    ucTrace() << LogStream::Color::Red << "nullptr thread";
    AbstractTask *_t = new AbstractThread::InternalTask([this]() { delete this; });
    if (!AbstractThread::currentThread()->invokeTask(_t)) {
      _t->invoke();
      delete _t;
    }
    return;
  }
  thread_->invokeMethod([this]() { delete this; });
  thread_->removeSocket(this);
  trace();
}

void AbstractSocket::pollEvent(int _e) {
  trace() << LogStream::Color::DarkMagenta << fd_ << static_cast<int>(_e) << static_cast<int>(state_);
  if (state_ == State::Listening) {
    incomingEvent();
    return;
  }
  if (_e & AbstractThread::PollHup) {
    if (state_ == State::Connecting) {
      private_->errorString_ = "Connection refused";
      private_->errorCode_   = Error::Refused;
    } else {
      private_->errorString_ = "Connection closed";
      private_->errorCode_   = Error::Closed;
    }
    ucTrace() << LogStream::Color::Red << private_->errorString_;
    errorEvent();
    close();
    return;
  }
  if (_e & AbstractThread::PollErr) {
    private_->errorString_ = "PollErr";
    private_->errorCode_   = Error::PollErr;
    ucDebug() << LogStream::Color::Red << private_->errorString_;
    errorEvent();
    close();
    return;
  }
  if (state_ == State::Connecting) {
    thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn);
    state_ = Connected;
    stateEvent();
  }
  if (state_ == State::Connected) {
    if (_e & AbstractThread::PollIn) {
      AbstractSocket::read_available_fd();
      if (rs_ < 0) {
        private_->errorString_ = "Connection closed (not accepted)";
        private_->errorCode_   = Error::Closed;
        ucDebug() << LogStream::Color::Red << private_->errorString_ << "not active" << errno;
        errorEvent();
        close();
        return;
      }
    }
    acceptEvent();
    if (state_ != State::Active || AbstractSocket::read_available_fd() <= 0) return;
  }
  if (_e & AbstractThread::PollIn) {
    int r = read_available_fd();
    if (rs_ < 0) {
      private_->errorString_ = "Connection finished";
      private_->errorCode_   = Error::Finished;
      ucTrace() << LogStream::Color::Red << private_->errorString_ << r << rs_ << errno;
      errorEvent();
      close();
      return;
    }
    if (r > 0) {
      readEvent();
      if (rs_ > 0) {
        do {
          private_->rda_.resize(private_->rda_.size() + rs_);
          int r = read_fd(private_->rda_.data() + private_->rda_.size() - rs_, rs_);
          if (r != rs_) {
            private_->errorString_ = "Read error";
            private_->errorCode_   = Error::Read;
            ucDebug() << LogStream::Color::Red << private_->errorString_ << r << rs_ << errno;
            errorEvent();
            close();
          }
        } while (read_available_fd() > 0);
      }
    } else if (r < 0) {
      private_->errorString_ = "Unknown error";
      private_->errorCode_   = Error::Unknown;
      ucDebug() << LogStream::Color::Red << private_->errorString_ << r << errno;
      errorEvent();
      close();
      return;
    }
  }
  if (_e & AbstractThread::PollOut) {
    writeEvent();
    if (private_->wda_.empty()) {
      private_->w_--;
      if (!private_->w_) thread_->modifyPollDescriptor(fd_, AbstractThread::PollIn);
      if (state_ == State::Closing) close();
      return;
    }
    int r = write_fd(private_->wda_.data(), private_->wda_.size());
    if (r > 0) {
      if (r < static_cast<int>(private_->wda_.size())) {
        private_->wda_.erase(private_->wda_.begin(), private_->wda_.begin() + r);
        return;
      }
      private_->wda_.clear();
      private_->w_ = 1;
    }
    if (r < 0) {
      private_->errorString_ = "Write error";
      private_->errorCode_   = Error::Write;
      ucDebug() << LogStream::Color::Red << private_->errorString_ << r << errno;
      errorEvent();
      close();
    }
  }
}

void ListenSocket::incomingEvent() {
  sockaddr_storage _a;
  socklen_t _l = sizeof _a;
  int _cd      = accept(fd_, (struct sockaddr *)&_a, &_l);
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
  if (_cd > 0) {
    if (!incomingConnection || !incomingConnection(_cd, _pa)) {
      ucDebug("failed incoming connection") << LogStream::Color::Red << (incomingConnection != nullptr) << _cd;
      close_fd(_cd);
      return;
    }
  }
  trace() << LogStream::Color::Red << _cd << LogStream::Color::Green << _pa;
}

void ListenSocket::setIncomingConnection(std::function<bool(int, const std::string &)> f) { incomingConnection = f; }
