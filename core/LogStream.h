#pragma once

#include <iostream>
#include <sstream>
#include <vector>
#include <format>
#include <cstdint>

//#include <cxxabi.h>

#define LOG_STREAM_CONSOLE_LEVEL 7

#define LOG_STREAM_CONSOLE_SENDER 0x01
#define LOG_STREAM_CONSOLE_SPACE 0x02
#define LOG_STREAM_CONSOLE_COLOR 0x04

#define LOG_STREAM_CONSOLE_EXTEND 0x10
#define LOG_STREAM_CONSOLE_LINE 0x20
#define LOG_STREAM_CONSOLE_ONLY 0x40

#define LOG_STREAM_FLUSH 0x80

#ifndef LOG_STREAM_DEFAULT_TIME_FORMAT
  #define LOG_STREAM_DEFAULT_TIME_FORMAT "%d.%m %H:%M:%S"
#endif

#ifndef LOG_DEFAULT_FLAGS
  #define LOG_DEFAULT_FLAGS (LOG_STREAM_CONSOLE_SENDER | LOG_STREAM_CONSOLE_SPACE | LOG_STREAM_CONSOLE_EXTEND)
#endif

#ifndef LS_LOG_DEFAULT_FLAGS
  #define LS_LOG_DEFAULT_FLAGS (LOG_STREAM_CONSOLE_SPACE | LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND)
#endif

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
    Message(uint8_t, const std::string &, const std::string &, const std::string &);
    Message(uint64_t time, uint8_t type, const std::string &name, const std::string &string, const std::string &note) : time(time), type(type), name(name), string(string), note(note) {}
    uint64_t time;
    uint8_t type;
    std::string name;
    std::string string;
    std::string note;
    bool operator==(const Message &m) const { return type == m.type && string == m.string && name == m.name && note == m.note; }
  };

  static void console_output(const Message &, uint8_t = LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND);
  static std::string levelName(uint8_t);
  static std::string colorString(Color);
  static std::string sender(const char *);
  static std::string timeString(const uint64_t, const std::string & = LOG_STREAM_DEFAULT_TIME_FORMAT, bool = false);
  static std::string currentTimeString(const std::string & = LOG_STREAM_DEFAULT_TIME_FORMAT, bool = false);
  inline static void (*completed)(const Message &, uint8_t) = &console_output;
  inline static void setCompleted(void (*_completed)(const Message &, uint8_t)) { completed = _completed; }

  LogStream(uint8_t, const char *, const char *, int, uint8_t = 0);
  LogStream() = default;
  ~LogStream() noexcept(false);
  LogStream &operator<<(decltype(std::endl<char, std::char_traits<char>>) &);
  LogStream &operator<<(const Color);
  LogStream &operator<<(const int8_t);
  LogStream &operator<<(const uint8_t);
  LogStream &operator<<(const char *);
  LogStream &operator<<(char *);
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
  LogStream &output(const std::string &);
  template <typename... Args>
  LogStream &output(std::format_string<Args...> msg, Args &&...args) {
    before();
    stream << std::vformat(msg.get(), std::make_format_args(args...));
    after();
    return *this;
  }
  LogStream &space();
  LogStream &nospace();
  LogStream &flush();
  static void setFunctionPrefixIgnoreList(const std::vector<std::string> &list) { functionPrefixIgnoreList_ = list; }
  static void setSenderPrefix(const std::string &prefix) { senderPrefix_ = prefix; }

private:
  void before();
  void after();
  std::string name;
  std::stringstream stream;
  uint8_t type;
  const char *file;
  int line;
  uint16_t flags;
  inline static std::vector<std::string> functionPrefixIgnoreList_;
  inline static std::string senderPrefix_;
};
}  // namespace AsyncFw

#define logTrace AsyncFw::LogStream(+AsyncFw::LogStream::Trace | AsyncFw::LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logDebug AsyncFw::LogStream(+AsyncFw::LogStream::Debug | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logInfo AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkGreen, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logNotice AsyncFw::LogStream(+AsyncFw::LogStream::Notice | AsyncFw::LogStream::Green, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logWarning AsyncFw::LogStream(+AsyncFw::LogStream::Warning | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logError AsyncFw::LogStream(+AsyncFw::LogStream::Error | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logAlert AsyncFw::LogStream(+AsyncFw::LogStream::Alert | AsyncFw::LogStream::Red, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output
#define logEmergency AsyncFw::LogStream(+AsyncFw::LogStream::Emergency | AsyncFw::LogStream::Red, __PRETTY_FUNCTION__, __FILE__, __LINE__, LOG_DEFAULT_FLAGS).output

#ifndef __GNUG__
  #ifdef _MSC_VER
    #define __PRETTY_FUNCTION__ __FUNCSIG__
  #else
    #define __PRETTY_FUNCTION__
  #endif
#endif

#ifndef LS_NO_TRACE
  #define lsTrace AsyncFw::LogStream(+AsyncFw::LogStream::Trace | AsyncFw::LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsTrace(...) \
    {}                 \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef LS_NO_DEBUG
  #define lsDebug AsyncFw::LogStream(+AsyncFw::LogStream::Debug | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsDebug(...) \
    {}                 \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef LS_NO_INFO
  #define lsInfo AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
  #define lsInfoRed AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
  #define lsInfoGreen AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkGreen, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
  #define lsInfoBlue AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
  #define lsInfoCyan AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkCyan, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
  #define lsInfoMagenta AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkMagenta, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsInfo(...) \
    {}                \
    if constexpr (0) AsyncFw::LogStream()
  #define lsInfoRed(...) \
    {}                   \
    if constexpr (0) AsyncFw::LogStream()
  #define lsInfoGreen(...) \
    {}                     \
    if constexpr (0) AsyncFw::LogStream()
  #define lsInfoBlue(...) \
    {}                    \
    if constexpr (0) AsyncFw::LogStream()
  #define lsInfoCyan(...) \
    {}                    \
    if constexpr (0) AsyncFw::LogStream()
  #define lsInfoMagenta(...) \
    {}                       \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef LS_NO_NOTICE
  #define lsNotice AsyncFw::LogStream(+AsyncFw::LogStream::Notice | AsyncFw::LogStream::Green, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsNotice(...) \
    {}                  \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef LS_NO_WARNING
  #define lsWarning AsyncFw::LogStream(+AsyncFw::LogStream::Warning | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsWarning(...) \
    {}                   \
    if constexpr (0) AsyncFw::LogStream()
#endif
#ifndef LS_NO_ERROR
  #define lsError AsyncFw::LogStream(+AsyncFw::LogStream::Error | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS).output
#else
  #define lsError(...) \
    {}                 \
    if constexpr (0) AsyncFw::LogStream()
#endif

#include "log-types.hpp"
