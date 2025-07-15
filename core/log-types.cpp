#include "log-types.hpp"
#include <NetworkInterface.hpp>

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

LogStream &operator<<(LogStream &log, const std::wstring &v) {
  if (v.empty()) {
    log << "\"\"";
    return log;
  }
  std::string str;
  str.resize(std::wcstombs(nullptr, v.data(), 0));
  std::wcstombs(str.data(), v.data(), str.size());
  log << str;
  return log;
}

LogStream &operator<<(LogStream &log, const DataStream &v) { return (log << v.array()); }

LogStream &operator<<(LogStream &log, const std::thread::id &v) { return (log << *static_cast<const std::thread::native_handle_type *>(static_cast<const void *>(&v))); }

#ifndef _WIN32
LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::MacAddress &v) { return (log << "interface: " + v.interface + ", mac: " + v.mac); }

LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::Address &v) { return (log << "interface: " + v.interface + ", ip: " + v.ip + ", mask: " + v.mask + ", broadcast/destination: " + v.broadcast); }

LogStream &operator<<(LogStream &log, const LRW::NetworkInterface::Route &v) { return (log << "interface: " + v.interface + ", gateway: " + v.gateway + ", net: " + v.net + ", mask: " + v.mask); }
#endif
}  // namespace AsyncFw
