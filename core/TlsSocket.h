/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "Socket.h"

namespace AsyncFw {
class TlsContext;

/*! \brief The AbstractTlsSocket class provides the base functionality for TLS encrypted socket. */
class AbstractTlsSocket : public AbstractSocket {
  friend LogStream &operator<<(LogStream &, const AbstractTlsSocket &);
  struct Private;

public:
  enum IgnoreErrors : uint8_t { TimeValidity = 0x01 };

  void setDescriptor(int) override;
  bool connect(const std::string &, uint16_t) override;
  void disconnect() override;
  void close() override;

  void setContext(const TlsContext &) const;

protected:
  AbstractTlsSocket();
  ~AbstractTlsSocket() override;

  virtual void activateReady();

  void activateEvent() override;

  int read_available_fd() const override final;
  int read_fd(void *_p, int _s) override final;
  int write_fd(const void *_p, int _s) override final;

private:
  Private *private_;
};
}  // namespace AsyncFw
