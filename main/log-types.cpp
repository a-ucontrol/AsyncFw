/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "core/LogStream.h"

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const std::wstring &v) {
  if (v.empty()) {
    log << "\"\"";
    return log;
  }
  std::string str;
  str.resize(std::wcstombs(nullptr, v.data(), 0));
  std::wcstombs(str.data(), v.data(), str.size());
  log << str;
  return log;
}
}  // namespace AsyncFw
