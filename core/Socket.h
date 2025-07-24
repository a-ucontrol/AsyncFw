#pragma once

#include <climits>  //INT_MAX
#include <functional>

#include "DataArray.h"
#include "AnyData.h"

namespace AsyncFw {
class SocketThread;

class AbstractSocket : public AnyData {
  friend SocketThread;
  friend class ListenSocket;
  friend class AbstractTlsSocket;
  friend class DataArraySocket;
  struct Private;

public:
  enum State : uint8_t { Unconnected, Listening, Connecting, Connected, Active, Closing };
  enum Error : uint8_t { None, Closed, Refused, PollErr, PollInval, Read, Write, Unknown };

  AbstractSocket(SocketThread * = nullptr);
  AbstractSocket(int, int, int, SocketThread * = nullptr);
  virtual ~AbstractSocket();

  virtual void setDescriptor(int);
  virtual bool connect(const std::string &, uint16_t);
  virtual void disconnect();
  virtual void destroy();
  virtual void close();

  bool listen(const std::string &, uint16_t);

  int read(uint8_t *, int);
  DataArray read(int = INT_MAX);
  int write(const uint8_t *, int);
  int write(const DataArray &);

  int errorCode();
  std::string errorString() const;

  int pendingRead() const;
  int pendingWrite() const;

  int descriptorWriteSize();

  State state() const { return state_; }
  SocketThread *thread() const { return thread_; }

  std::string address() const;
  uint16_t port() const;
  std::string peerAddress() const;
  uint16_t peerPort() const;

protected:
  void setErrorCode(int);
  void setErrorString(const std::string &);

  virtual int read_available_fd() const;
  virtual int read_fd(void *_p, int _s);
  virtual int write_fd(const void *_p, int _s);

  virtual void acceptEvent();
  virtual void errorEvent();

  virtual void incomingEvent() {}
  virtual void writeEvent() {}

  virtual void stateEvent() = 0;
  virtual void readEvent() = 0;

  void changeDescriptor(int);

  mutable State state_ = Unconnected;
  SocketThread *thread_;

private:
  void pollEvent(int);
  Private *private_;
  int fd_ = 0;
  mutable int rs_ = 0;
};

class ListenSocket : public AbstractSocket {
public:
  using AbstractSocket::AbstractSocket;
  void setIncomingConnection(std::function<bool(int, const std::string &)> f);

protected:
  void incomingEvent() override;
  void stateEvent() override {}
  void readEvent() override {}

private:
  std::function<bool(int, const std::string &)> incomingConnection;
};
}  // namespace AsyncFw
