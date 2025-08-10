#pragma once

#include <sstream>
#include <cstdint>
#include <vector>

#if __has_include(<format>)
  #include <format>
#endif
#ifndef __cpp_lib_format
  #include <cstdarg>
#endif

//#include <cxxabi.h>

//#define STD_FOMAT_TIME_STRING
#define LOG_STREAM_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

#define LOG_STREAM_CONSOLE_LEVEL 7

//#define LOG_STREAM_CONSOLE_SENDER 0x01
//#define LOG_STREAM_CONSOLE_SPACE  0x02
#define LOG_STREAM_CONSOLE_COLOR 0x04

#define LOG_STREAM_CONSOLE_EXTEND 0x10
#define LOG_STREAM_CONSOLE_NOTE 0x20
#define LOG_STREAM_CONSOLE_LINE LOG_STREAM_CONSOLE_NOTE
#define LOG_STREAM_CONSOLE_ONLY 0x40

namespace AsyncFw {
class LogStream {
public:
  enum MessageType : uint8_t {
    Emergency = 0x00,
    Alert = 0x01,
    Error = 0x02,
    Warning = 0x03,
    Notice = 0x04,
    Info = 0x05,
    Debug = 0x06,
    Trace = 0x07,
  };

  enum Output : uint8_t {
    NoConsole = 0x08,
  };

  //color names
  //https://doc.qt.io/qt-6/qcolor.html, Predefined Colors
  enum Color : uint8_t {
    White = 0x10,
    Gray = 0x20,
    Black = 0x30,
    Red = 0x40,
    Green = 0x50,
    Blue = 0x60,
    Cyan = 0x70,
    Magenta = 0x80,
    Yellow = 0x90,
    DarkRed = 0xa0,
    DarkGreen = 0xb0,
    DarkBlue = 0xc0,
    DarkCyan = 0xd0,
    DarkMagenta = 0xe0,
    DarkYellow = 0xf0,
  };

public:
  struct Message {
    Message() = default;
    Message(uint8_t type, const std::string &name, const std::string &string, const std::string &note);
    Message(uint64_t time, uint8_t type, const std::string &name, const std::string &string, const std::string &note) : time(time), type(type), name(name), string(string), note(note) {}
    uint64_t time;
    uint8_t type;
    std::string name;
    std::string string;
    std::string note;
    bool operator==(const Message &m) const { return type == m.type && string == m.string && name == m.name && note == m.note; }
  };

  static void console_output(const Message &, uint8_t = LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND);
  static std::string levelName(uint8_t l);
  static std::string colorString(Color c);
  static std::string sender(const char *function);
#ifdef STD_FOMAT_TIME_STRING
  static std::string timeString(const uint64_t time, const char *format = "{:%m.%d %H:%M:%OS}");
#else
  static std::string timeString(const uint64_t time, const char *format = "%d.%m %H:%M:%S");
#endif
  inline static void (*completed)(const Message &, uint8_t) = &console_output;
  inline static void setCompleted(void (*_completed)(const Message &, uint8_t)) { completed = _completed; }

  LogStream(uint8_t type, const char *function, const char *file, int line, uint8_t flags = 0);
  LogStream() = default;
  ~LogStream();
  LogStream &operator<<(decltype(std::endl<char, std::char_traits<char>>) &);
  LogStream &operator<<(const Color val);
  LogStream &operator<<(const char *val);
  LogStream &operator<<(char *val);
  template <typename T>
  inline LogStream &operator<<(T val) {
    typedef const typename std::remove_reference<T>::type type;
    before();
    if constexpr (std::is_same<type, const std::string>::value || std::is_same<type, const std::string_view>::value) {
      if (val.empty()) {
        stream << "\"\"";
        after();
        return *this;
      }
    }
    //int status;
    //char *name = abi::__cxa_demangle(typeid(T).name(), NULL, NULL, &status);
    //*stream << '-' << name << '-';
    stream << std::forward<T>(val);
    after();
    return *this;
  }
  LogStream &output() { return *this; }
  LogStream &output(const std::string &msg);
#ifndef __cpp_lib_format
  LogStream &output(const char *msg, ...) {
    char str[256];
    va_list list;
    va_start(list, msg);
    int r = vsnprintf(str, sizeof(str), msg, list);
    va_end(list);
    before();
    stream << str;
    if (r >= static_cast<int>(sizeof(str))) { stream << "..."; }
    after();
    return *this;
  }
#else
  template <typename... Args>
  LogStream &output(std::format_string<Args...> msg, Args &&...args) {
    before();
    if (msg.get().find('{') != std::string::npos) stream << std::vformat(msg.get(), std::make_format_args(args...));
    else {
      char str[256];
      int r = snprintf(str, sizeof(str), msg.get().data(), args...);
      stream << str;
      if (r >= static_cast<int>(sizeof(str))) { stream << "..."; }
    }
    after();
    return *this;
  }
#endif
  LogStream &space();
  LogStream &nospace();
  static void setSenderPrefixIgnoreList(const std::vector<std::string> &list) { senderPrefixIgnoreList_ = list; }

private:
  void before();
  void after();
  std::string name;
  std::stringstream stream;
  uint8_t type;
  const char *file;
  int line;
  uint16_t flags;
  inline static std::vector<std::string> senderPrefixIgnoreList_;
};
}  // namespace AsyncFw

#define logTrace AsyncFw::LogStream(+AsyncFw::LogStream::Trace | AsyncFw::LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logDebug AsyncFw::LogStream(+AsyncFw::LogStream::Debug | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logInfo AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkGreen, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logNotice AsyncFw::LogStream(+AsyncFw::LogStream::Notice | AsyncFw::LogStream::Green, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logWarning AsyncFw::LogStream(+AsyncFw::LogStream::Warning | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logError AsyncFw::LogStream(+AsyncFw::LogStream::Error | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logAlert AsyncFw::LogStream(+AsyncFw::LogStream::Alert | AsyncFw::LogStream::Red, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output
#define logEmergency AsyncFw::LogStream(+AsyncFw::LogStream::Emergency | AsyncFw::LogStream::Red, __PRETTY_FUNCTION__, __FILE__, __LINE__, 3).output

#ifndef __GNUG__
  #ifdef _MSC_VER
    #define __PRETTY_FUNCTION__ __FUNCSIG__
  #else
    #define __PRETTY_FUNCTION__
  #endif
#endif

#ifndef uC_NO_TRACE
  #define ucTrace AsyncFw::LogStream(+AsyncFw::LogStream::Trace | AsyncFw::LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
#else
  #define ucTrace(...) \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef uC_NO_DEBUG
  #define ucDebug AsyncFw::LogStream(+AsyncFw::LogStream::Debug | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
#else
  #define ucDebug(...) \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef uC_NO_INFO
  #define ucInfo AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
  #define ucInfoRed AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
  #define ucInfoGreen AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkGreen, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
  #define ucInfoBlue AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
  #define ucInfoCyan AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkCyan, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
  #define ucInfoMagenta AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkMagenta, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
#else
  #define ucInfo(...) \
    if constexpr (0) AsyncFw::LogStream()
  #define ucInfoRed(...) \
    if constexpr (0) AsyncFw::LogStream()
  #define ucInfoGreen(...) \
    if constexpr (0) AsyncFw::LogStream()
  #define ucInfoBlue(...) \
    if constexpr (0) AsyncFw::LogStream()
  #define ucInfoCyan(...) \
    if constexpr (0) AsyncFw::LogStream()
  #define ucInfoMagenta(...) \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef uC_NO_WARNING
  #define ucWarning AsyncFw::LogStream(+AsyncFw::LogStream::Warning | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
#else
  #define ucWarning(...) \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef uC_NO_ERROR
  #define ucError AsyncFw::LogStream(+AsyncFw::LogStream::Error | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6).output
#else
  #define ucError(...) \
    if constexpr (0) AsyncFw::LogStream()
#endif

#include "log-types.hpp"
