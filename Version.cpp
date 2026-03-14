/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "Version.h"

std::string AsyncFw::Version::str() { return VERSION_STRING; }

std::string AsyncFw::Version::git() { return GIT_VERSION; }
