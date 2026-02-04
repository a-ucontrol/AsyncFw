#include "Thread.h"
#include "Socket.h"
#include "DataArray.h"
#include "TlsContext.h"
#include "LogStream.h"

#define LOG_DATA_ARAY_SIZE_LIMIT 4096

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArray &v) { return log << v.view(); }
LogStream &operator<<(LogStream &log, const DataArrayView &v) {
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
  log << static_cast<std::string_view>(v);
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

LogStream &operator<<(LogStream &log, const TlsContext &v) { return (log << v.infoKey() << std::endl << v.infoCertificate() << std::endl << v.infoTrusted() << std::endl << v.verifyName()); }

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
