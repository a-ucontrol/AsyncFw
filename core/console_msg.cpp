/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#undef emit
#include <iostream>
#include <syncstream>
#include "console_msg.hpp"

void AsyncFw::console_msg_(const std::string &m) { (std::osyncstream(std::cerr) << m << std::endl).flush(); }
