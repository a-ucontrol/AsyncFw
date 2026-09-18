/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>
namespace AsyncFw {
void console_msg_(const std::string &);
}

#ifdef LS_NO_WARNING
  #define console_msg(x, y) AsyncFw::console_msg_(std::string(x) + ": " + y)
#else
  #define console_msg(x, y) ((void)0)
#endif
