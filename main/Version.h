/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Version.h @brief The Version class. */

#include <string>

namespace AsyncFw {
/** @struct Version Version.h <AsyncFw/Version> @brief Provides access to compile-time software versioning metrics and Git repository metadata.
@details This structure exposes static helper methods to fetch semantic version strings and detailed build tags generated during the compilation pipeline. */
struct Version {
  /** @brief Returns a numerical version string based on the closest tag and commit distance.
  @details Extracts the version formatted as Major.Minor.Patch.CommitCount, where the final digit represents the exact number of commits made after the last baseline tag.
  @return A string containing the core version layout (e.g., "1.3.2.7"). */
  static std::string str();
  /** @brief Returns detailed build configuration tags, including Git repository tracking metadata.
  @note If the resulting string ends with the "-m" suffix, it indicates that the binary was compiled from a "dirty" working directory containing uncommitted changes.
  @warning If the source code is built directly from a released source archive/tarball (e.g., downloaded from https://github.com/a-ucontrol/AsyncFw/archive/refs/tags/v1.3.2.tar.gz), the underlying Git repository metadata will be unavailable, and this method will return "unknown".
  @return A string containing full Git deployment details (e.g., "main/v1.3.2-7-g5362f7fd-m"). */
  static std::string git();
};
}  // namespace AsyncFw
