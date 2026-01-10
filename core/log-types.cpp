#include "log-types.hpp"
#include "Thread.h"
#include "Socket.h"

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

LogStream &operator<<(LogStream &log, const AbstractThread &t) { return (log << "name:" << t.name() << "id:" << t.id() << "running:" << t.running()); }

LogStream &operator<<(LogStream &log, const AbstractSocket &s) { return (log.nospace() << "local: " << s.address() << ':' << std::to_string(s.port()) << " peer: " << s.peerAddress() << ':' << s.peerPort() << " state: " << static_cast<int>(s.state())); }

LogStream &operator<<(LogStream &log, const std::thread::id &v) { return (log << *static_cast<const std::thread::native_handle_type *>(static_cast<const void *>(&v))); }

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
}  // namespace AsyncFw
