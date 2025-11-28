#pragma once

#undef emit

#include <iostream>
#include <syncstream>

#ifndef LS_NO_WARNING
  #define console_msg(x) \
    do { (std::osyncstream(std::cerr) << std::string() + x << std::endl).flush(); } while (0)
#else
  #define console_msg(x)                    \
    if constexpr (0) do {                   \
        std::string _s = std::string() + x; \
        (void)_s;                           \
    } while (0)
#endif
