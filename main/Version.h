/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>

namespace AsyncFw {
/*! \struct Version Version.h <AsyncFw/Version> \brief The Version struct */
struct Version {
  static std::string str();
  static std::string git();
};
}  // namespace AsyncFw
