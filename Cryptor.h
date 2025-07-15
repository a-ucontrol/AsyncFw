#pragma once

#include <AsyncFw/core/DataArray.h>

namespace AsyncFw {
class Cryptor {
public:
  static bool encrypt(const DataArray &key, const DataArray &text, DataArray &ctext);
  static bool decrypt(const DataArray &key, const DataArray &ctext, DataArray &text);

protected:
  static void encrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &text, DataArray &ctext);
  static void decrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &ctext, DataArray &text);
};

}  // namespace LRW
