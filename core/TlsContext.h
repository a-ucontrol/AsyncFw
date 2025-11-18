#pragma once

#include "DataArray.h"

struct ssl_ctx_st;
struct x509_store_ctx_st;
namespace std {
class mutex;
}
namespace AsyncFw {
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

  DataArray key() const;
  DataArray certificate() const;
  DataArrayList trusted() const;

  bool setKey(const DataArray &);
  bool setCertificate(const DataArray &);
  bool appendTrusted(const DataArray &);
  bool setDefaultVerifyPaths();

  bool empty() const;
  bool verifyCertificate() const;

  bool generateKey(int = 2048);
  bool generateCertificate(const std::vector<std::pair<std::string, std::string>> & = {{"CN", "Root-CA"}}, const std::string & = {}, const std::string & = "CA:TRUE,pathlen:1", int = 365);
  DataArray generateRequest(const std::vector<std::pair<std::string, std::string>> &, const std::string & = {}, const std::string & = {} /*for ca: "CA:TRUE,pathlen:0"*/);
  DataArray signRequest(DataArray &, int = 365);

  std::string commonName() const;

  std::string infoKey() const;
  std::string infoCertificate() const;
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
}  // namespace AsyncFw
