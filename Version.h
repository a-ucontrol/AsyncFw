#pragma once

#include <string>

namespace AsyncFw {
struct Version {
  static std::string str();
  static std::string git();
};
}  // namespace AsyncFw
