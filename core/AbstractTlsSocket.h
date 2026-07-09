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
@details Extends the basic AbstractSocket interface to introduce cryptographic session wraps. It abstracts OpenSSL state transitions, handles non-blocking TLS server/client handshakes, intercepts read/write pipelines, and enforces peer identity validation logic.
@brief Example: @snippet Socket/main.cpp snippet */
class AbstractTlsSocket : public AbstractSocket {
  friend LogStream &operator<<(LogStream &, const AbstractTlsSocket &);

public:
  /** @enum IgnoreErrors @brief Bitmask definitions for bypassing specific validation errors during the TLS handshake. */
  enum IgnoreErrors : uint8_t {
    TimeValidity = 0x01 /**< Ignore certificate expiration boundaries or invalid systemic timestamp validation. */
  };
  /** @brief Binds a raw native operating system socket descriptor to this managed context.
  @note Automatically configures the socket to act as a TLS server if a valid OpenSSL context is present.
  @param fd Native socket descriptor integer. */
  void setDescriptor(int) override;
  /** @brief Initiates an outbound client TCP connection, automatically determining the TLS role.
  @note Automatically activates client-side TLS mode if a valid OpenSSL context is assigned.
  @param address Target host IP address or domain name. @param port Target remote network service port. @return true if the underlying socket successfully started the connection sequence, otherwise false. */
  bool connect(const std::string &, uint16_t) override;
  /** @brief Shuts down the active TLS session gracefully via SSL_shutdown() before terminating the network pipe. */
  void disconnect() override;
  /** @brief Immediately releases OpenSSL session pointers and forcefully closes the system-level descriptor. */
  void close() override;
  /** @brief Assigns a copy of the security credentials context containing certificates, keys, and rules. @param context Immutable reference to the target TlsContext configuration. */
  void setContext(const TlsContext &) const;
  /** @brief Checks whether the underlying OpenSSL context structure has not been initialized or assigned. @return true if the internal TlsContext is considered empty or invalid, otherwise false. */
  bool contextEmpty() const;

protected:
  /** @brief Protected default constructor. Allocates the internal private structure and setups context pointers. */
  AbstractTlsSocket();
  /** @brief Virtual destructor. Disposes of allocated OpenSSL instances and clean private data maps. */
  ~AbstractTlsSocket() override;
  /** @brief Intermediate signal hub executed immediately when a secure cryptographic tunnel has been fully established.
  @details Overrides should call this method to propagate ready events up to packet parser layers. */
  virtual void activateReady();
  /** @brief Core internal event handler reacting to raw descriptor readiness signals.
  @details Orchestrates the non-blocking state machine for SSL_accept() and SSL_connect() loops. Automatically intercepts peer certificates and flags errors if handshakes time out or crash. */
  void activateEvent() override;
  /** @brief Calculates how many unread bytes are currently waiting within the active OpenSSL decrypted layer buffer. @return Byte amount available for reading */
  int read_available_fd() const override final;
  /** @brief Low-level decryption routing path proxying requests down to SSL_read(). @param data Void pointer targeting the destination extraction memory chunk. @param size Maximum byte capacity boundaries allowed to extract. @return Number of successfully decrypted bytes ingested, or standard OpenSSL system error codes. */
  int read_fd(void *, int) override final;
  /** @brief Low-level encryption routing path proxying outgoing payload directly up to SSL_write(). @param data Void pointer targeting the source serialization raw bytes memory buffer. @param size Exact byte amount metrics to push into the socket layer. @return Number of successfully encrypted and dispatched bytes, or standard OpenSSL system error codes. */
  int write_fd(const void *, int) override final;

private:
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
