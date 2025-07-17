#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/core_names.h>
#include <openssl/x509v3.h>
#include <mutex>

#include "LogStream.h"

#include "TlsContext.h"

using namespace AsyncFw;

struct TlsContext::Private {
  Private() { ctx_ = SSL_CTX_new(TLS_method()); }
  ~Private() { SSL_CTX_free(ctx_); }
  SSL_CTX *ctx_;

  static DataArray key(EVP_PKEY *);
  static DataArray certificate(X509 *);
  static std::string info(EVP_PKEY *);
  static std::string info(X509 *);

  std::string verifyName_;
  uint8_t ignoreErrors_ = 0;

  int serial_ = 0;
  int ref_    = 0;
};

DataArray TlsContext::Private::key(EVP_PKEY *_k) {
  BIO *_bio = BIO_new(BIO_s_mem());
  PEM_write_bio_PrivateKey(_bio, _k, nullptr, nullptr, 0, nullptr, nullptr);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return _da;
}

DataArray TlsContext::Private::certificate(X509 *_c) {
  BIO *_bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(_bio, _c);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return _da;
}

std::string TlsContext::Private::info(EVP_PKEY *_k) {
  BIGNUM *bnk = nullptr;
  EVP_PKEY_get_bn_param(_k, OSSL_PKEY_PARAM_RSA_N, &bnk);
  BIO *_bio = BIO_new(BIO_s_mem());
  ASN1_bn_print(_bio, "Modulus:", bnk, nullptr, 0);
  BN_free(bnk);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return std::string(_da.view());
}

std::string TlsContext::Private::info(X509 *_c) {
  BIO *_bio = BIO_new(BIO_s_mem());
  X509_print(_bio, _c);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return std::string(_da.view());
}

TlsContext::TlsContext() { private_ = new Private; }

TlsContext::TlsContext(const TlsContext &_d) {
  private_ = _d.private_;
  private_->ref_++;
}

TlsContext::TlsContext(const DataArray &k, const DataArray &c, const DataArrayList &t, const std::string &n, uint8_t e) : TlsContext() {
  setKey(k);
  setCertificate(c);
  for (const DataArray &_c : t) appendTrusted(_c);
  if (!n.empty()) setVerifyName(n);
  if (e) setIgnoreErrors(e);
}

TlsContext::~TlsContext() {
  if (private_->ref_) private_->ref_--;
  else { delete private_; }
}

TlsContext &TlsContext::operator=(const TlsContext &_d) {
  _d.private_->ref_++;
  if (private_->ref_) private_->ref_--;
  else { delete private_; }
  private_ = _d.private_;
  return *this;
}

bool TlsContext::empty() const {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (_k) return false;
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (_c) return false;
  X509_STORE *_store = SSL_CTX_get_cert_store(private_->ctx_);
  STACK_OF(X509) *_t = X509_STORE_get1_all_certs(_store);
  int _s             = sk_X509_num(_t);
  sk_X509_free(_t);
  if (_s) return false;
  return true;
}

bool TlsContext::generateKey(int bits) {
  EVP_PKEY *_k = EVP_RSA_gen(bits);
  if (!_k) {
    ucError() << "error generate key";
    return false;
  }
  int r = SSL_CTX_use_PrivateKey(private_->ctx_, _k);
  EVP_PKEY_free(_k);
  return r == 1;
}

bool TlsContext::generateCertificate(const std::vector<std::pair<std::string, std::string>> &subject, const std::string &san, bool ca, int days) {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return false;
  }
  X509 *_c = X509_new();
  if (!_c) {
    ucError() << "error allocate memory";
    return false;
  }
  X509_set_version(_c, 2);
  ASN1_INTEGER_set(X509_get_serialNumber(_c), ++private_->serial_);
  X509_gmtime_adj(X509_get_notBefore(_c), 0);
  X509_gmtime_adj(X509_get_notAfter(_c), days * 24 * 3600L);
  X509_set_pubkey(_c, _k);
  X509_NAME *name = X509_get_subject_name(_c);
  for (const std::pair<std::string, std::string> &_e : subject) { X509_NAME_add_entry_by_txt(name, _e.first.c_str(), MBSTRING_ASC, reinterpret_cast<const unsigned char *>(_e.second.c_str()), -1, -1, 0); }
  X509_set_issuer_name(_c, name);
  X509_EXTENSION *ext = nullptr;
  if (!san.empty()) {
    ext = X509V3_EXT_conf(NULL, NULL, SN_subject_alt_name, san.c_str());
    if (!X509_add_ext(_c, ext, -1)) {
      X509_EXTENSION_free(ext);
      X509_free(_c);
      return false;
    }
    X509_EXTENSION_free(ext);
  }
  if (ca) {
    ext = X509V3_EXT_conf(NULL, NULL, SN_basic_constraints, "CA:TRUE,pathlen:1");
    if (!X509_add_ext(_c, ext, -1)) {
      X509_EXTENSION_free(ext);
      return false;
    }
    X509_EXTENSION_free(ext);
  }
  if (!X509_sign(_c, _k, EVP_sha256())) {
    ucError() << "error signing certificate";
    X509_free(_c);
    return false;
  }
  int r = SSL_CTX_use_certificate(private_->ctx_, _c);
  X509_free(_c);
  return r == 1;
}

DataArray TlsContext::generateRequest(const std::vector<std::pair<std::string, std::string>> &subject, const std::string &san, bool ca) {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return {};
  }
  X509_REQ *_r = X509_REQ_new();
  if (!_r) {
    ucError() << "error allocate memory";
    return {};
  }
  X509_REQ_set_pubkey(_r, _k);
  X509_NAME *name = X509_REQ_get_subject_name(_r);
  for (const std::pair<std::string, std::string> &_e : subject) { X509_NAME_add_entry_by_txt(name, _e.first.c_str(), MBSTRING_ASC, reinterpret_cast<const unsigned char *>(_e.second.c_str()), -1, -1, 0); }
  STACK_OF(X509_EXTENSION) *extlist = sk_X509_EXTENSION_new_null();
  if (!extlist) {
    sk_X509_EXTENSION_free(extlist);
    return {};
  }
  X509_EXTENSION *ext_ca = nullptr;
  if (ca) {
    ext_ca = X509V3_EXT_conf(NULL, NULL, SN_basic_constraints, "CA:TRUE,pathlen:0");
    if (!ext_ca) {
      sk_X509_EXTENSION_free(extlist);
      return {};
    };
    sk_X509_EXTENSION_push(extlist, ext_ca);
  }
  X509_EXTENSION *ext_san = nullptr;
  if (!san.empty()) {
    ext_san = X509V3_EXT_conf(NULL, NULL, SN_subject_alt_name, san.c_str());
    if (!ext_san) {
      if (ext_ca) X509_EXTENSION_free(ext_ca);
      sk_X509_EXTENSION_free(extlist);
      return {};
    };
    sk_X509_EXTENSION_push(extlist, ext_san);
  }
  if (!X509_REQ_add_extensions(_r, extlist)) {
    if (ext_ca) X509_EXTENSION_free(ext_ca);
    if (ext_san) X509_EXTENSION_free(ext_san);
    sk_X509_EXTENSION_free(extlist);
    return {};
  }
  sk_X509_EXTENSION_free(extlist);
  if (ext_san) X509_EXTENSION_free(ext_san);
  if (ext_ca) X509_EXTENSION_free(ext_ca);
  if (!X509_REQ_sign(_r, _k, EVP_sha256())) {
    ucError() << "error signing certificate";
    EVP_PKEY_free(_k);
    X509_REQ_free(_r);
    return {};
  }
  BIO *_bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509_REQ(_bio, _r);
  X509_REQ_free(_r);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return _da;
}

DataArray TlsContext::signRequest(DataArray &req, int days) {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return {};
  }
  X509 *_rc = X509_new();
  if (!_rc) {
    ucError() << "error allocate memory";
    return {};
  }
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (!_c) {
    ucError() << "error get certificate";
    return {};
  }
  BIO *_bio      = BIO_new_mem_buf(req.data(), req.size());
  X509_REQ *_req = PEM_read_bio_X509_REQ(_bio, nullptr, nullptr, nullptr);
  BIO_free(_bio);
  if (!_req) {
    ucError() << "error read request";
    return {};
  }
  EVP_PKEY *_rk = X509_REQ_get_pubkey(_req);
  if (!_rk) {
    ucError() << "error read req key";
    return {};
  }
  X509_set_version(_rc, 2);
  ASN1_INTEGER_set(X509_get_serialNumber(_rc), ++private_->serial_);
  X509_gmtime_adj(X509_get_notBefore(_rc), 0);
  X509_gmtime_adj(X509_get_notAfter(_rc), days * 24 * 3600L);
  X509_set_pubkey(_rc, _rk);
  EVP_PKEY_free(_rk);
  X509_set_issuer_name(_rc, X509_get_subject_name(_c));
  X509_set_subject_name(_rc, X509_REQ_get_subject_name(_req));
  STACK_OF(X509_EXTENSION) *extlist = X509_REQ_get_extensions(_req);
  if (!extlist) {
    sk_X509_EXTENSION_free(extlist);
    return {};
  }
  int _s = sk_X509_EXTENSION_num(extlist);
  for (int i = 0; i < _s; i++) {
    X509_EXTENSION *_e = sk_X509_EXTENSION_value(extlist, i);
    if (!X509_add_ext(_rc, _e, -1)) {
      X509_EXTENSION_free(_e);
      sk_X509_EXTENSION_free(extlist);
      return {};
    }
    X509_EXTENSION_free(_e);
  }
  sk_X509_EXTENSION_free(extlist);
  X509_REQ_free(_req);
  if (!X509_sign(_rc, _k, EVP_sha256())) {
    ucError() << "error signing certificate";
    X509_free(_rc);
    return {};
  }
  _bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(_bio, _rc);
  X509_free(_rc);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return _da;
}

std::string TlsContext::commonName() const {
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (!_c) {
    ucError() << "error get certificate";
    return {};
  }
  char name[256];
  X509_NAME_get_text_by_NID(X509_get_subject_name(_c), NID_commonName, name, sizeof(name));
  return name;
}

DataArray TlsContext::key() const {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return {};
  }
  return Private::key(_k);
}

DataArray TlsContext::certificate() const {
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (!_c) {
    ucError() << "error get certificate";
    return {};
  }
  return Private::certificate(_c);
}

DataArrayList TlsContext::trusted() const {
  X509_STORE *_store = SSL_CTX_get_cert_store(private_->ctx_);
  STACK_OF(X509) *_t = X509_STORE_get1_all_certs(_store);
  int _s             = sk_X509_num(_t);
  DataArrayList _l;
  for (int i = 0; i < _s; i++) {
    X509 *_c = sk_X509_value(_t, i);
    _l.push_back(Private::certificate(_c));
  }
  sk_X509_free(_t);
  return _l;
}

std::string TlsContext::infoKey() const {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return {};
  }
  return Private::info(_k);
}

std::string TlsContext::infoCertificate() const {
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (!_c) {
    ucError() << "error get certificate";
    return {};
  }
  return Private::info(_c);
}

std::string TlsContext::infoTrusted() const {
  X509_STORE *_store = SSL_CTX_get_cert_store(private_->ctx_);
  STACK_OF(X509) *_t = X509_STORE_get1_all_certs(_store);
  std::string str;
  int _s = sk_X509_num(_t);
  for (int i = 0; i < _s; i++) {
    X509 *_c = sk_X509_value(_t, i);
    str += Private::info(_c);
  }
  sk_X509_free(_t);
  return str;
}

std::string TlsContext::infoRequest(const DataArray &req) {
  BIO *_bio    = BIO_new_mem_buf(req.data(), req.size());
  X509_REQ *_r = PEM_read_bio_X509_REQ(_bio, NULL, NULL, NULL);
  BIO_free(_bio);
  _bio = BIO_new(BIO_s_mem());
  X509_REQ_print(_bio, _r);
  X509_REQ_free(_r);
  DataArray _da;
  _da.resize(BIO_ctrl_pending(_bio));
  BIO_read(_bio, _da.data(), _da.size());
  BIO_free(_bio);
  return std::string(_da.view());
}

std::string &TlsContext::verifyName() const { return private_->verifyName_; }

void TlsContext::setVerifyName(const std::string &name) const { private_->verifyName_ = name; }

uint8_t TlsContext::ignoreErrors() const { return private_->ignoreErrors_; }

void TlsContext::setIgnoreErrors(uint8_t errors) const { private_->ignoreErrors_ = errors; }

ssl_ctx_st *TlsContext::opensslCtx() const { return private_->ctx_; }

bool TlsContext::verify() const {
  EVP_PKEY *_k = SSL_CTX_get0_privatekey(private_->ctx_);
  if (!_k) {
    ucError() << "error get key";
    return {};
  }
  X509 *_c = SSL_CTX_get0_certificate(private_->ctx_);
  if (!_c) {
    ucError() << "error get certificate";
    return false;
  }
  EVP_PKEY *_ck = X509_get_pubkey(_c);
  if (!_ck) {
    ucError() << "error get certificate pubkey";
    return false;
  }
  BIGNUM *bnk = nullptr;
  EVP_PKEY_get_bn_param(_k, OSSL_PKEY_PARAM_RSA_N, &bnk);
  BIGNUM *bnc = nullptr;
  EVP_PKEY_get_bn_param(_ck, OSSL_PKEY_PARAM_RSA_N, &bnc);
  EVP_PKEY_free(_ck);
  bool result = BN_cmp(bnk, bnc) == 0;
  BN_free(bnk);
  BN_free(bnc);
  return result;
}

bool TlsContext::setKey(const DataArray &_da) {
  BIO *_bio    = BIO_new_mem_buf(_da.data(), _da.size());
  EVP_PKEY *_k = PEM_read_bio_PrivateKey(_bio, nullptr, nullptr, nullptr);
  BIO_free(_bio);
  if (!_k) return false;
  int r = SSL_CTX_use_PrivateKey(private_->ctx_, _k);
  EVP_PKEY_free(_k);
  if (r == 1) return true;
  ucError() << LogStream::Color::Red << ERR_error_string(ERR_get_error(), nullptr);
  return false;
}

bool TlsContext::setCertificate(const DataArray &_da) {
  BIO *_bio = BIO_new_mem_buf(_da.data(), _da.size());
  X509 *_c  = PEM_read_bio_X509(_bio, NULL, NULL, NULL);
  BIO_free(_bio);
  if (!_c) return false;
  int r = SSL_CTX_use_certificate(private_->ctx_, _c);
  X509_free(_c);
  if (r == 1) return true;
  ucError() << LogStream::Color::Red << ERR_error_string(ERR_get_error(), nullptr);
  return false;
}

bool TlsContext::appendTrusted(const DataArray &_da) {
  BIO *_bio = BIO_new_mem_buf(_da.data(), _da.size());
  X509 *_c  = PEM_read_bio_X509(_bio, NULL, NULL, NULL);
  BIO_free(_bio);
  if (!_c) return false;
  X509_STORE *_store = SSL_CTX_get_cert_store(private_->ctx_);
  int r              = X509_STORE_add_cert(_store, _c);
  X509_free(_c);
  if (r == 1) return true;
  ucError() << LogStream::Color::Red << ERR_error_string(ERR_get_error(), nullptr);
  return false;
}
