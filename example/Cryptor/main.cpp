/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/Cryptor>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  std::string _key = "012345678901234567890123456789++";
  std::string _str =
      "#include <AsyncFw/MainThread>\n"
      "#include <AsyncFw/Cryptor>\n"
      "#include <AsyncFw/LogStream>";

  AsyncFw::DataArray _encrypted;
  AsyncFw::DataArray _decrypted;
  bool _r = AsyncFw::Cryptor::encrypt(_key, _str, _encrypted);

  lsDebug() << _r << std::endl << _encrypted;

  _r = AsyncFw::Cryptor::decrypt(_key, _encrypted, _decrypted);

  lsInfoGreen() << _r << std::endl << _decrypted;

  return 0;
}
//! [snippet]
