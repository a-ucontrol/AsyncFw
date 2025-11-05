#include <iostream>
#include <regex>
#include <syncstream>

#include "core/console_msg.hpp"
#include "LogStream.h"

#define LOG_STREAM_EMERGENCY_TERMINATE

#define LOG_STREAM_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

#define STD_FOMAT_TIME_STRING

#ifndef STD_FOMAT_TIME_STRING
  #include <iomanip>
  #include <time.h>
#else
  #include <chrono>
#endif

using namespace AsyncFw;
void LogStream::console_output(const Message &message, uint8_t flags) {
  if (!(flags & 0x80) && (message.type & 0x07) > LOG_STREAM_CONSOLE_LEVEL) return;
  std::string _message = message.string;
  std::string str;
  if (flags & LOG_STREAM_CONSOLE_COLOR) { str = colorString(static_cast<Color>(message.type & 0xF8)); }
  int i = message.type & 0x07;
  if (flags & LOG_STREAM_CONSOLE_EXTEND) {
    str += timeString(message.time) + " " + levelName(i) + ": ";
    if (!message.name.empty()) { str += message.name; }
    if (!message.string.empty() && !message.name.empty()) {
      if (message.string.find('\n') == std::string::npos) str += " -> ";
      else {
        str += "\n ";
        _message = regex_replace(_message, std::regex("\n"), "\n ");
      }
    }
  } else {
    if (message.string.empty() && message.note.empty()) return;
  }
  if (!message.string.empty()) str += _message;
  if (flags & LOG_STREAM_CONSOLE_LINE && !message.note.empty()) {
    if (flags & LOG_STREAM_CONSOLE_COLOR) str += "\x1b[3;90m";
    str += "\n" + message.note;
  }
  if (flags & LOG_STREAM_CONSOLE_COLOR) str += "\x1b[0m";

  std::ostream *out = (i != Info && i != Notice) ? &std::cerr : &std::cout;

#ifdef Q_OS_WIN
  str = QString::fromStdString(str).toLocal8Bit().toStdString();
#endif

  std::osyncstream(*out) << str << std::endl;
  if (flags & 0x80) std::osyncstream(*out).flush();
}

LogStream::Message::Message(uint8_t type, const std::string &name, const std::string &string, const std::string &note) : type(type), name(name), string(string), note(note) { time = LOG_STREAM_CURRENT_TIME; }

std::string LogStream::sender(const char *function) {
  if (!function) return "Unknown";
  std::string str(function);
  if (str.find("{anonymous}") != std::string::npos) return "Anonymous";
  std::size_t i;
  str = str.substr(0, str.find_first_of('<'));
  str = str.substr(0, str.find_first_of('('));
  i = str.find_last_of(' ');
  if (i != std::string::npos) str = str.substr(i + 1);
  for (const std::string &_str : senderPrefixIgnoreList_) {
    if (str.starts_with(_str)) {
      str.erase(0, _str.size());
      break;
    }
  }
  i = str.find_first_of("::");
  if (i == std::string::npos) return "Application";
  str = str.substr(0, i);
  return str;
}

std::string LogStream::timeString(const uint64_t time, const std::string &format, bool show_ms) {
#ifdef STD_FOMAT_TIME_STRING
  if (!show_ms) {
    std::chrono::zoned_time _zt {std::chrono::current_zone(), std::chrono::sys_time<std::chrono::seconds> {std::chrono::seconds(time / 1000)}};
    return std::vformat("{:" + format + '}', std::make_format_args(_zt));
  }
  std::chrono::zoned_time _zt {std::chrono::current_zone(), std::chrono::sys_time<std::chrono::milliseconds> {std::chrono::milliseconds(time)}};
  return std::vformat("{:" + format + '}', std::make_format_args(_zt));
#else
  std::string str;
  std::time_t t = time / 1000;
  #ifndef _WIN32
  struct tm tm;
  str += (std::stringstream() << std::put_time(localtime_r(&t, &tm), format.c_str())).str();
  #else
  str += (std::stringstream() << std::put_time(localtime(&t), format.c_str())).str();
  #endif
  if (show_ms) {
    int i = time % 1000;
    str += ".";
    if (i < 100) str += "0";
    if (i < 10) str += "0";
    str += std::to_string(i);
  }
  return str;
#endif
}

std::string LogStream::currentTimeString(const std::string &format, bool show_ms) { return timeString(LOG_STREAM_CURRENT_TIME, format, show_ms); }

std::string LogStream::levelName(uint8_t l) {
  if (l == Trace) return "trace";
  else if (l == Debug)
    return "debug";
  else if (l == Info)
    return "info";
  else if (l == Notice)
    return "notice";
  else if (l == Warning)
    return "warning";
  else if (l == Error)
    return "error";
  else if (l == Alert)
    return "alert";
  else if (l == Emergency)
    return "emergency";
  return "unknown";
}

std::string LogStream::colorString(Color c) {
  if (c == 0) {
    return "\x1b[0m";
  } else if (c == White)
    return "\x1b[1;37m";
  else if (c == Gray)
    return "\x1b[0;37m";
  else if (c == Black)
    return "\x1b[1;30m";
  else if (c == Red)
    return "\x1b[1;31m";
  else if (c == Green)
    return "\x1b[1;32m";
  else if (c == Blue)
    return "\x1b[1;34m";
  else if (c == Cyan)
    return "\x1b[1;36m";
  else if (c == Magenta)
    return "\x1b[1;35m";
  else if (c == Yellow)
    return "\x1b[1;33m";
  else if (c == DarkRed)
    return "\x1b[0;31m";
  else if (c == DarkGreen)
    return "\x1b[0;32m";
  else if (c == DarkBlue)
    return "\x1b[0;34m";
  else if (c == DarkCyan)
    return "\x1b[0;36m";
  else if (c == DarkMagenta)
    return "\x1b[0;35m";
  else if (c == DarkYellow)
    return "\x1b[0;33m";
  return {};
}

LogStream::LogStream(uint8_t type, const char *function, const char *file, int line, uint8_t flags) : type(type), file(file), line(line), flags(flags) {
  if (flags & 0x0001) name = sender(function);
  else { name = function; }
}

LogStream::~LogStream() {
#ifdef LOG_STREAM_EMERGENCY_TERMINATE
  if ((type & 0x07) == Emergency) {
    completed({type, name, ((flags & 0x8000) ? stream.str() : ""), std::string(file) + ":" + std::to_string(line)}, flags | LOG_STREAM_CONSOLE_EXTEND | LOG_STREAM_FLUSH);
    std::cerr << "Log level Emergency, terminate..." << std::endl;
    std::abort();
  } else
#endif
    completed({type, name, ((flags & 0x8000) ? stream.str() : ""), std::string(file) + ":" + std::to_string(line)}, flags | LOG_STREAM_CONSOLE_EXTEND);
}

LogStream &LogStream::operator<<(decltype(std::endl<char, std::char_traits<char>>) &) {
  stream << std::endl;
  flags &= ~0x4000;
  return *this;
}

LogStream &LogStream::operator<<(const Color val) {
  if (!(flags & 0x04)) return *this;
  stream << colorString(val);
  flags |= 0x0100;
  return *this;
}

LogStream &LogStream::operator<<(int8_t val) {
  before();
  stream << static_cast<int>(val);
  after();
  return *this;
}

LogStream &LogStream::operator<<(uint8_t val) {
  before();
  stream << static_cast<unsigned int>(val);
  after();
  return *this;
}

LogStream &LogStream::operator<<(const char *val) {
  before();
  if (!val) stream << "NULL";
  else if (*val == 0)
    stream << "\"\"";
  else
    stream << val;
  after();
  return *this;
}

LogStream &LogStream::operator<<(char *val) {
  before();
  if (!val) stream << "NULL";
  else if (*val == 0)
    stream << "\"\"";
  else
    stream << val;
  after();
  return *this;
}

LogStream &LogStream::output(const std::string &msg) {
  before();
  stream << msg;
  after();
  return *this;
}

LogStream &LogStream::space() {
  flags |= 0x02;
  return *this;
}

LogStream &LogStream::nospace() {
  flags &= ~0x02;
  return *this;
}

LogStream &LogStream::flush() {
#ifndef uC_NO_TRACE
  console_msg("LogStream: flush flag enable");
#endif
  flags |= LOG_STREAM_FLUSH;
  return *this;
}

void LogStream::before() {
  if ((flags & 0x4002) == 0x4002) stream << ' ';
}

void LogStream::after() {
  if (flags & 0x0100) {
    stream << colorString(static_cast<Color>(type & 0xf0));
    flags &= ~0x0100;
  }
  flags |= 0xC000;
}
