#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "core/LogStream.h"

#include "Cryptor.h"

using namespace AsyncFw;
using EVP_CIPHER_CTX_free_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&::EVP_CIPHER_CTX_free)>;

void Cryptor::encrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &text, DataArray &ctext) {
  if (key.size() != 32 || iv.size() != 16) throw std::runtime_error("Key or iv size incorrect");
  EVP_CIPHER_CTX_free_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
  int rc = EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(iv.data()));
  if (rc != 1) throw std::runtime_error("EVP_EncryptInit_ex failed");

  // Recovered text expands upto BLOCK_SIZE (iv.size())
  ctext.resize(text.size() + iv.size());
  int out_len1 = ctext.size();

  rc = EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(ctext.data()), &out_len1, reinterpret_cast<const unsigned char *>(text.data()), text.size());
  if (rc != 1) throw std::runtime_error("EVP_EncryptUpdate failed");

  int out_len2 = ctext.size() - out_len1;
  rc = EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(ctext.data()) + out_len1, &out_len2);
  if (rc != 1) throw std::runtime_error("EVP_EncryptFinal_ex failed");

  // Set cipher text size now that we know it
  ctext.resize(out_len1 + out_len2);
}

void Cryptor::decrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &ctext, DataArray &text) {
  if (key.size() != 32 || iv.size() != 16) throw std::runtime_error("Key or iv size incorrect");
  EVP_CIPHER_CTX_free_ptr ctx(EVP_CIPHER_CTX_new(), ::EVP_CIPHER_CTX_free);
  int rc = EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), NULL, reinterpret_cast<const unsigned char *>(key.data()), reinterpret_cast<const unsigned char *>(iv.data()));
  if (rc != 1) throw std::runtime_error("EVP_DecryptInit_ex failed");

  // Recovered text contracts upto BLOCK_SIZE (iv.size())
  text.resize(ctext.size());
  int out_len1 = text.size();

  rc = EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(text.data()), &out_len1, reinterpret_cast<const unsigned char *>(ctext.data()), (int)ctext.size());
  if (rc != 1) throw std::runtime_error("EVP_DecryptUpdate failed");

  int out_len2 = text.size() - out_len1;
  rc = EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(text.data()) + out_len1, &out_len2);
  if (rc != 1) throw std::runtime_error("EVP_DecryptFinal_ex failed");

  // Set recovered text size now that we know it
  text.resize(out_len1 + out_len2);
}

bool Cryptor::encrypt(const DataArray &key, const DataArray &text, DataArray &ctext) {
  DataArray iv;
  iv.resize(16);
  int rc = RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), iv.size());
  if (rc != 1) {
    ucError("RAND_bytes for iv failed");
    return false;
  }
  try {
    encrypt(key, iv.view(), text.view(), ctext);
  } catch (const std::exception &e) {
    ucError() << e.what();
    return false;
  }
  ctext += iv;
  return true;
}

bool Cryptor::decrypt(const DataArray &key, const DataArray &ctext, DataArray &text) {
  std::size_t i = 16;
  try {
    decrypt(key, DataArrayView(ctext.data() + ctext.size() - i, i), DataArrayView(ctext.data(), ctext.size() - i), text);
  } catch (const std::exception &e) {
    ucDebug() << e.what();
    return false;
  }
  return true;
}
