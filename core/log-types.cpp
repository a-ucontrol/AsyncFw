#include "log-types.hpp"

#define LOG_DATA_ARAY_SIZE_LIMIT 4096

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArray &v) {
  if (v.empty()) {
    log << "[]";
    return log;
  }
  if (v.size() > LOG_DATA_ARAY_SIZE_LIMIT) {
    log << "[Large data, size: " + std::to_string(v.size()) + "]";
    return log;
  }
  for (const uint8_t &c : v)
    if (!std::isprint(c) && c != '\n' && c != '\r') {
      log << "[Binary data, size: " + std::to_string(v.size()) + "]";
      return log;
    }
  log << v.view();
  return log;
}

LogStream &operator<<(LogStream &log, const DataArrayList &v) {
  if (v.empty()) {
    log << "[[]]";
    return log;
  }
  log << "[[Items: " + std::to_string(v.size()) + "]]";
  return log;
}

LogStream &operator<<(LogStream &log, const DataStream &v) { return (log << v.array()); }

LogStream &operator<<(LogStream &log, const std::thread::id &v) { return (log << *static_cast<const std::thread::native_handle_type *>(static_cast<const void *>(&v))); }

}  // namespace AsyncFw
