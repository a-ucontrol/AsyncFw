#ifndef LS_NO_WARNING

  #undef emit
  #include <iostream>
  #include <syncstream>
  #include "console_msg.hpp"

void AsyncFw::console_msg_(const std::string &m) { (std::osyncstream(std::cerr) << m << std::endl).flush(); }

#endif
