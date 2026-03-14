/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

namespace AsyncFw {
class DataArray;
class DataArrayView;
class Cryptor {
public:
  static bool encrypt(const DataArray &key, const DataArray &text, DataArray &ctext);
  static bool decrypt(const DataArray &key, const DataArray &ctext, DataArray &text);

protected:
  static void encrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &text, DataArray &ctext);
  static void decrypt(const DataArray &key, const DataArrayView &iv, const DataArrayView &ctext, DataArray &text);
};
}  // namespace AsyncFw
