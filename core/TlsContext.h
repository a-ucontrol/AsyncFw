/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ssl_ctx_st;
struct x509_store_ctx_st;

namespace AsyncFw {
class DataArray;
class DataArrayList;
class LogStream;
/*! \class TlsContext TlsContext.h <AsyncFw/TlsContext> \brief Manages cryptographic configurations, security profiles, credentials, and certificates for TLS sessions.
\brief TlsContext encapsulates the shared state required to establish secure Transport Layer Security (TLS) connections. It acts as a centralized repository for loading public key infrastructure (PKI) certificates, private keys, trusted Certificate Authorities (CAs), and configuring allowed cipher suites and TLS protocol versions.
\brief Example: \snippet TlsContext/main.cpp snippet */
class TlsContext {
  friend class AbstractTlsSocket;
  struct Private;

public:
  TlsContext();
  TlsContext(const DataArray &k, const DataArray &c, const DataArrayList &t, const std::string & = {}, uint8_t = 0);
  TlsContext(const TlsContext &);
  TlsContext(const TlsContext &&) = delete;
  ~TlsContext();
  TlsContext &operator=(const TlsContext &);

  /*! \brief Return private key in PEM format */
  DataArray key() const;
  /*! \brief Return certificate in PEM format */
  DataArray certificate() const;
  /*! \brief Return list of trusted certificates in PEM format */
  DataArrayList trusted() const;

  /*! \brief Set private key */
  bool setKey(const DataArray &);
  /*! \brief Set certificate */
  bool setCertificate(const DataArray &);
  /*! \brief Append trusted certificate */
  bool appendTrusted(const DataArray &);

  bool setDefaultVerifyPaths();

  /*! \brief Return True if context empty */
  bool empty() const;
  /*! \brief Return True if certificate verified */
  bool verifyCertificate() const;

  /*! \brief Generate private key. \param bits number of bits in the generate key \return True if generated */
  bool generateKey(int = 2048);
  /*! \brief Generate certificate. \param subject subject \param san subject alternative name \param ca ca \param days days \return True if generated */
  bool generateCertificate(const std::vector<std::pair<std::string, std::string>> & = {{"CN", "Root-CA"}}, const std::string & = {}, const std::string & = "CA:TRUE,pathlen:1", int = 365);
  DataArray generateRequest(const std::vector<std::pair<std::string, std::string>> &, const std::string & = {}, const std::string & = {} /*for ca: "CA:TRUE,pathlen:0"*/);
  DataArray signRequest(DataArray &, int = 365);

  /*! \brief Return certificate common name */
  std::string commonName() const;

  /*! \brief Return info of private key */
  std::string infoKey() const;
  /*! \brief Return info of certificate */
  std::string infoCertificate() const;
  /*! \brief Return info of trusted certificates */
  std::string infoTrusted() const;

  static std::string infoKey(const DataArray &);
  static std::string infoCertificate(const DataArray &);
  static std::string infoRequest(const DataArray &);

  static std::string errorString();
  static std::string allErrorStrings();

  bool verifyPeer();
  void setVerifyPeer(bool);
  std::string &verifyName() const;
  void setVerifyName(const std::string &) const;
  void setIgnoreErrors(uint8_t) const;

protected:
  static int verify(int ok, x509_store_ctx_st *ctx);
  ssl_ctx_st *opensslCtx() const;

private:
  Private *private_;
};
LogStream &operator<<(LogStream &, const TlsContext &);
}  // namespace AsyncFw
