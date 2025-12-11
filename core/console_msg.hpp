#pragma once

#include <string>
namespace AsyncFw {
void console_msg_(const std::string &);
}

#ifndef LS_NO_WARNING
  #define console_msg(x) AsyncFw::console_msg_(std::string() + x)
#else
  #define console_msg(x) \
    if constexpr (0) (void)(std::string() + x)
#endif
