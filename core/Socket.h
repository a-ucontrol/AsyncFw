#pragma once

#include <functional>

#include "DataArray.h"
#include "AnyData.h"

namespace AsyncFw {
class Thread;

class AbstractSocket : public AnyData {
  friend Thread;
  friend class ListenSocket;
  friend class AbstractTlsSocket;
  friend class DataArraySocket;
  struct Private;

public:
  enum class State : uint8_t { Unconnected, Listening, Connecting, Connected, Active, Closing, Destroy, Error };
  enum class Error : uint8_t { None, Closed, Refused, PollErr, PollInval, Read, Write, Accept, Unknown };

  virtual void setDescriptor(int);
  virtual bool connect(const std::string &, uint16_t);
  virtual void disconnect();
  virtual void close();

  bool listen(const std::string &, uint16_t);

  void destroy();

  DataArray &peek();
  int read(uint8_t *, int);
  DataArray read(int = 0);
  int write(const uint8_t *, int);
  int write(const DataArray &);

  Error errorCode();
  std::string errorString() const;

  int pendingRead() const;
  int pendingWrite() const;

  int descriptorWriteSize();

  State state() const { return state_; }
  Thread *thread() const { return thread_; }

  std::string address() const;
  uint16_t port() const;
  std::string peerAddress() const;
  uint16_t peerPort() const;

protected:
  AbstractSocket(Thread * = nullptr);
  AbstractSocket(int, int, int, Thread * = nullptr);
  virtual ~AbstractSocket();

  void setErrorCode(Error);
  void setErrorString(const std::string &) const;

  virtual int read_available_fd() const;
  virtual int read_fd(void *_p, int _s);
  virtual int write_fd(const void *_p, int _s);

  virtual void acceptEvent();

  virtual void incomingEvent() {}
  virtual void writeEvent() {}

  virtual void stateEvent() = 0;
  virtual void readEvent() = 0;

  mutable State state_ = State::Unconnected;
  Thread *thread_;

private:
  void pollEvent(int);
  void changeDescriptor(int);
  void read_fd();
  Private *private_;
  int fd_ = -1;
  mutable int rs_ = 0;
};

class ListenSocket : public AbstractSocket {
public:
  ListenSocket(Thread *_t = nullptr) : AbstractSocket(_t) {}
  void setIncomingConnection(std::function<bool(int, const std::string &)> f);

protected:
  void incomingEvent() override;
  void stateEvent() override {}
  void readEvent() override {}

private:
  std::function<bool(int, const std::string &)> incomingConnection;
};
}  // namespace AsyncFw
