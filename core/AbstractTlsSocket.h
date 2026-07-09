/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file AbstractTlsSocket.h @brief The AbstractTlsSocket class. */

#include "AbstractSocket.h"

namespace AsyncFw {
class TlsContext;

/** @class AbstractTlsSocket AbstractTlsSocket.h <AsyncFw/AbstractTlsSocket> @brief Abstract base class providing Transport Layer Security (TLS/SSL) encryption layers on top of standard network sockets.
@brief AbstractSocket serves as the core cryptographic wrapper interface. It manages secure session initiation, manages incoming/outgoing encrypted packet streams, handles TLS handshakes, and provides mechanisms for public-key infrastructure (PKI) certificate verification.
@brief Example: @snippet Socket/main.cpp snippet */
class AbstractTlsSocket : public AbstractSocket {
  friend LogStream &operator<<(LogStream &, const AbstractTlsSocket &);

public:
  enum IgnoreErrors : uint8_t { TimeValidity = 0x01 };

  void setDescriptor(int) override;
  bool connect(const std::string &, uint16_t) override;
  void disconnect() override;
  void close() override;

  void setContext(const TlsContext &) const;
  bool contextEmpty() const;

protected:
  AbstractTlsSocket();
  ~AbstractTlsSocket() override;

  virtual void activateReady();

  void activateEvent() override;

  int read_available_fd() const override final;
  int read_fd(void *_p, int _s) override final;
  int write_fd(const void *_p, int _s) override final;

private:
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
