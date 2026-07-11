/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file TlsContext.h @brief The TlsContext class. */

#include <string>
#include <vector>
#include <cstdint>

struct ssl_ctx_st;
struct x509_store_ctx_st;

namespace AsyncFw {
class DataArray;
class DataArrayList;
class LogStream;
/** @class TlsContext TlsContext.h <AsyncFw/TlsContext> @brief Manages cryptographic configurations, security profiles, credentials, and certificates for TLS sessions.
@details TlsContext encapsulates the shared state required to establish secure Transport Layer Security (TLS) connections. It acts as a centralized repository for loading public key infrastructure (PKI) certificates, private keys, and trusted Certificate Authorities (CAs).
@warning Thread Affinity & Immutability Contract:
This class is **NOT thread-safe for modifications**. It must be fully configured (keys loaded, rules set) within a single thread *BEFORE* being assigned to any active network socket. Once attached to a socket, the context **MUST be treated as strictly Immutable (Read-Only)**. Concurrent writes during active I/O will cause catastrophic Data Races and Undefined Behavior.
@brief Example: @snippet TlsContext/main.cpp snippet */
class TlsContext {
  friend class AbstractTlsSocket;
  struct Private;

public:
  /** @enum IgnoreErrors @brief Bitmask definitions for bypassing specific validation errors during the TLS handshake. */
  enum IgnoreErrors : uint8_t {
    None = 0x00,
    TimeValidity = 0x01 /**< Ignore certificate expiration boundaries or invalid systemic timestamp validation. */
  };
  TlsContext();
  TlsContext(const DataArray &, const DataArray &, const DataArrayList &, const std::string & = {}, IgnoreErrors = None);
  TlsContext(const TlsContext &);
  TlsContext(const TlsContext &&) = delete;
  ~TlsContext();
  TlsContext &operator=(const TlsContext &);

  /** @brief Return private key in PEM format */
  DataArray key() const;
  /** @brief Return certificate in PEM format */
  DataArray certificate() const;
  /** @brief Return list of trusted certificates in PEM format */
  DataArrayList trusted() const;

  /** @brief Set private key
  @warning Must only be called during initialization phase. Never call this method if the context is already shared with active sockets. */
  bool setKey(const DataArray &);
  /** @brief Set certificate
  @warning Must only be called during initialization phase. Never call this method if the context is already shared with active sockets. */
  bool setCertificate(const DataArray &);
  /** @brief Append trusted certificate
  @note Safe to call during initial configuration. Modifying trusted root CAs on the fly for active sockets is strictly prohibited. */
  bool appendTrusted(const DataArray &);

  bool setDefaultVerifyPaths();

  /** @brief Return True if context empty */
  bool empty() const;
  /** @brief Return True if certificate verified */
  bool verifyCertificate() const;

  /** @brief Generate private key. @param bits Number of bits in the generate key @return True if generated */
  bool generateKey(int = 2048);
  /** @brief Generate certificate. @param subject Subject. @param san Subject alternative name. @param ca CA certificate. @param days The certificate validity period in days. @return True if generated */
  bool generateCertificate(const std::vector<std::pair<std::string, std::string>> & = {{"CN", "Root-CA"}}, const std::string & = {}, const std::string & = "CA:TRUE,pathlen:1", int = 365);
  /** @brief Generates a Certificate Signing Request (CSR). @param subject Certificate subject fields. @param san Subject Alternative Name string. @param extensions Additional X509v3 extensions. Example for CA: "CA:TRUE,pathlen:0". @return A DataArray containing the generated CSR in PEM format. */
  DataArray generateRequest(const std::vector<std::pair<std::string, std::string>> &, const std::string & = {}, const std::string & = {});
  /** @brief Signs an external Certificate Signing Request (CSR) using the current context's private key.
  @details This method acts as a local Certificate Authority (CA). It extracts the public key and subject from the provided CSR, validates the integrity, and signs it to issue a brand-new X509 certificate.
  @param requestBuffer Reference to the DataArray containing the raw CSR in PEM format. @param days The lifetime duration of the newly issued certificate in days (defaults to 1 year). @return A DataArray containing the signed X509 certificate in PEM format. */
  DataArray signRequest(DataArray &, int = 365);

  /** @brief Return certificate common name */
  std::string commonName() const;

  /** @brief Return info of private key */
  std::string infoKey() const;
  /** @brief Return info of certificate */
  std::string infoCertificate() const;
  /** @brief Return info of trusted certificates */
  std::string infoTrusted() const;

  /** @brief Parses a raw PEM-formatted private key buffer and extracts its diagnostic properties. @param key DataArray holding the raw private key bytes in PEM format. @return A descriptive text string detailing the key type (RSA/EC), bit strength, and parameters. */
  static std::string infoKey(const DataArray &);
  /** @brief Parses a raw PEM-formatted X509 certificate buffer and extracts its metadata header. @param certificate DataArray holding the raw certificate bytes in PEM format. @return A multi-line string containing the Subject, Issuer, Validity boundaries, and Serial Number. */
  static std::string infoCertificate(const DataArray &);
  /** @brief Parses a raw PEM-formatted Certificate Signing Request (CSR) and decodes its payload properties. @param request DataArray holding the raw CSR bytes in PEM format. @return A human-readable summary of the requested subject DN attributes and public key properties. */
  static std::string infoRequest(const DataArray &);

  /** @brief Fetches the most recent thread-local diagnostic error string recorded by the OpenSSL library stack. @return A description of the top error on the stack, or an empty string if clear. */
  static std::string errorString();
  /** @brief Unwinds and flushes the entire OpenSSL global error queue into a single concatenated text report.
  @details Iteratively calls ERR_get_error() until empty. Ideal for printing a comprehensive post-mortem report following a major handshake crash.
  @return A structured text block containing all pending OpenSSL error codes and reasons. */
  static std::string allErrorStrings();

  /** @brief Checks whether remote peer certificate validation is currently enabled. @return True if the context forces verification of the remote host's certificate chain. */
  bool verifyPeer();
  /** @brief Enables or disables remote peer certificate validation.
  @details When set to true, forces OpenSSL to validate the remote host's certificate against the loaded trusted CAs during the TLS handshake.
  @warning Thread Affinity: Must only be called during the initialization phase before the context is shared with active network sockets.
  @param enable True to enforce strict peer verification, false to skip validation (not recommended for production). */
  void setVerifyPeer(bool);
  /** @brief Returns the expected hostname configured for remote peer validation. @return A reference to the string containing the target domain name or Common Name (CN). */
  std::string &verifyName() const;
  /** @brief Sets the expected hostname or domain name to validate against the remote peer's certificate.
  @details Configures the context to strictly match the remote server's Common Name (CN) or Subject Alternative Name (SAN) fields with the provided string, preventing identity spoofing.
  @warning Thread Affinity: Must only be called during the initialization phase. Modifying this on the fly for active sockets causes critical data races.
  @param hostname The exact domain name or host IP string expected from the remote peer. */
  void setVerifyName(const std::string &) const;
  /** @brief Configures specific handshake validation bypass flags.
  @warning This method modifies the global internal verification registry. Calling it concurrently or post-initialization will corrupt the routing state. */
  void setIgnoreErrors(IgnoreErrors) const;

protected:
  /** @brief Returns a pointer to the underlying native OpenSSL context structure.
  @details This method acts as an escape hatch providing direct access to the raw SSL_CTX handle. It allows internal framework components or customized cryptographic extensions to invoke low-level OpenSSL API operations directly.
  @warning Direct modification of the returned context outside the initialization phase bypasses thread-safety guarantees and will cause undefined behavior.
  @return A pointer to the native ssl_ctx_st (SSL_CTX) structure handled by OpenSSL. */
  ssl_ctx_st *opensslCtx() const;

private:
  /* Low-level OpenSSL verification bridge callback invoked during the TLS handshake process.
  This method intercepts standard X509 certificate validation stages. It maps the raw OpenSSL context back to the corresponding TlsContext instance to evaluate user-defined security profiles, check hostnames, and enforce custom error bypass masks (e.g., IgnoreErrors). */
  static int verify(int, x509_store_ctx_st *);
  Private *private_;
};
LogStream &operator<<(LogStream &, const TlsContext &);
}  // namespace AsyncFw
