#pragma once

#include "DataArray.h"

struct ssl_ctx_st;
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

  bool empty() const;
  bool verify() const;

  bool generateKey(int = 2048);
  bool generateCertificate(const std::vector<std::pair<std::string, std::string>> & = {{"CN", "Root-CA"}}, const std::string & = {}, bool = true, int = 365);
  DataArray generateRequest(const std::vector<std::pair<std::string, std::string>> &, const std::string & = {}, bool = false);
  DataArray signRequest(DataArray &, int = 365);

  std::string commonName() const;

  std::string infoKey() const;
  std::string infoCertificate() const;
  std::string infoTrusted() const;
  static std::string infoRequest(const DataArray &);

  std::string verifyName() const;
  void setVerifyName(const std::string &) const;
  uint8_t ignoreErrors() const;
  void setIgnoreErrors(uint8_t) const;

  void lock() const;
  void unlock() const;

private:
  Private *private_;
  ssl_ctx_st *opensslCtx() const;
};
}  // namespace AsyncFw
