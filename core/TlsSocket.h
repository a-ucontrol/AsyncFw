#pragma once

#include "Socket.h"

namespace AsyncFw {
class TlsContext;

class AbstractTlsSocket : public AbstractSocket {
  struct Private;

public:
  enum IgnoreErrors : uint8_t { TimeValidity = 0x01 };

  void setDescriptor(int) override;
  bool connect(const std::string &, uint16_t) override;
  void disconnect() override;
  void close() override;

  void setContext(const TlsContext &) const;

protected:
  AbstractTlsSocket(Thread * = nullptr);
  ~AbstractTlsSocket() override;

  void acceptEvent() override;

  int read_available_fd() const override final;
  int read_fd(void *_p, int _s) override final;
  int write_fd(const void *_p, int _s) override final;

private:
  Private *private_;
};
}  // namespace AsyncFw
