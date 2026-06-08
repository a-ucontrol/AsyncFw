/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Cryptor.h @brief The Cryptor class. */

namespace AsyncFw {
class DataArray;
class DataArrayView;

/** @class Cryptor Cryptor.h <AsyncFw/Cryptor> @brief Utility class providing static interfaces for high-performance symmetric data cryptography.
@details Wraps low-level cryptographic backends (e.g., OpenSSL cipher suites) into a clean, type-safe interface operating with the framework's DataArray and DataArrayView containers. Natively supports automated Initialization Vector (IV) generation and secure block padding. @brief Example: @snippet Cryptor/main.cpp snippet */
class Cryptor {
public:
  /** @brief Type-safely encrypts raw data payload into a secure ciphertext buffer.
  @details Accepts input parameters via lightweight DataArrayView to avoid expensive memory allocations and copies when passing strings, raw buffers, or vector slices.
  @param key The symmetric encryption key view (e.g., must be 32 bytes for AES-256). @param text The source raw unencrypted input text or binary byte buffer view. @param ctext Out-parameter array container where the computed encrypted data stream will be appended. @return True if the cipher cryptographic context successfully initialized and finalized. */
  static bool encrypt(const DataArrayView &, const DataArrayView &, DataArray &);

  /** @brief Type-safely decrypts ciphertext back into the original raw data layout.
  @details Accepts input parameters via lightweight DataArrayView to maximize performance during block parsing and secure package unpacking.
  @param key The symmetric decryption key view matching the one used for encryption. @param ctext The input encrypted ciphertext byte buffer sequence view. @param text Out-parameter array container where the original decrypted data will be recovered and appended. @return True if decryption and block padding validation succeeded without corruption. */
  static bool decrypt(const DataArrayView &, const DataArrayView &, DataArray &);

protected:
  /** @brief Internal low-level block encryption engine accepting an explicit Initialization Vector (IV) token view. @param key Symmetric key buffer view reference. @param iv Immutable light block view referencing the specific Initialization Vector bytes. @param text Immutable light block view referencing the source unencrypted memory sequence. @param ctext Pre-allocated data block array targeting the final cipher payload destination. */
  static void encrypt(const DataArrayView &, const DataArrayView &, const DataArrayView &, DataArray &);

  /** @brief Internal low-level block decryption engine accepting an explicit Initialization Vector (IV) token view. @param key Symmetric key buffer view reference. @param iv Immutable light block view referencing the specific Initialization Vector bytes. @param ctext Immutable light block view referencing the input encrypted ciphertext sequence. @param text Pre-allocated data block array targeting the final decrypted plaintext destination. */
  static void decrypt(const DataArrayView &, const DataArrayView &, const DataArrayView &, DataArray &);
};
}  // namespace AsyncFw
