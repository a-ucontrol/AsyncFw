/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file LogStream.h @brief The LogStream class. */

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
  #define LOG_STREAM_DEFAULT_TIME_FORMAT "%Y-%m-%d %H:%M:%S"
#endif

#ifndef LOG_DEFAULT_FLAGS
  #define LOG_DEFAULT_FLAGS (LOG_STREAM_CONSOLE_SENDER | LOG_STREAM_CONSOLE_SPACE | LOG_STREAM_CONSOLE_EXTEND)
#endif

#ifndef LS_DEFAULT_FLAGS
  #define LS_DEFAULT_FLAGS (LOG_STREAM_CONSOLE_SPACE | LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND)
#endif

namespace AsyncFw {
/** @class LogStream LogStream.h <AsyncFw/LogStream> @brief A high-performance, thread-safe logging stream utility utilizing RAII execution semantics.
@details LogStream provides dynamic string and data formatting using standard C++ stream insertion operators (operator<<). The logged content is accumulated in an internal buffer and is guaranteed to flush automatically to the configured outputs when the temporary LogStream instance is destroyed at the end of the statement (RAII).
@exception std::runtime_error Thrown automatically if the log level is Emergency.
@note Throwing a log with Emergency level will automatically raise a std::runtime_error.
@note Please refer to the **example** for compile-time log optimizations (like LS_NO_TRACE) and standard inline vs formatting syntax usage styles.
@brief Example: @snippet snippet.dox LogStream */
class LogStream {
public:
  enum MessageType : uint8_t {
    Emergency = 0x00,  ///< Critical failure. Crashes the app with an exception.
    Alert = 0x01,      ///< Action must be taken immediately.
    Error = 0x02,      ///< Error conditions.
    Warning = 0x03,    ///< Warning conditions.
    Notice = 0x04,     ///< Normal but significant messages
    Info = 0x05,       ///< Informational messages.
    Debug = 0x06,      ///< Debug-level messages.
    Trace = 0x07,      ///< Deep trace-level messages.
  };

  enum Output : uint8_t {
    NoConsole = 0x08,
  };

  /** @enum Color @brief ANSI Terminal colors to toggle inline styling inside logs. */
  enum Color : uint8_t {
    Default = 0x00,
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

  /** @brief Complete container representing an unrolled, processed log entry. */
  struct Message {
    Message() = default;
    Message(uint8_t, const std::string &, const std::string &, const std::string &);
    Message(uint64_t time, uint8_t type, const std::string &name, const std::string &string, const std::string &note) : time(time), type(type), name(name), string(string), note(note) {}
    uint64_t time;       ///< Unix timestamp in milliseconds.
    uint8_t type;        ///< The log message severity level (MessageType).
    std::string name;    ///< Extracted signature/sender identification context.
    std::string string;  ///< Main log message body payload text.
    std::string note;    ///< Debug context notes (source file code trace location).
    bool operator==(const Message &m) const { return type == m.type && string == m.string && name == m.name && note == m.note; }
  };

  class TimeFormat {
    friend LogStream;

  public:
    TimeFormat();
    TimeFormat(const std::string &, bool = false);
    bool empty() const { return empty_; }

  private:
    std::string str;
    bool show_ms;
    bool empty_;
  };

  /** @brief Programmatically forwards a raw pre-built Message structure to the active logging router. */
  static void message(const Message &m, uint8_t f = LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND) { data.completed(m, f); }
  static void console_output(const Message &, uint8_t = LOG_STREAM_CONSOLE_COLOR | LOG_STREAM_CONSOLE_EXTEND);
  static std::string levelName(uint8_t);
  static std::string colorString(Color);
  static std::string sender(const char *);
  static std::string timeString(const uint64_t, const TimeFormat & = {});
  static std::string currentTimeString(const TimeFormat & = {});

  /** @brief Globally changes the datetime presentation format across all incoming log streams. */
  static void setTimeFormat(const std::string &, bool = false);
  /** @brief Adjusts the timestamp output offset (in seconds) to account for target time zones.
  @details **By default, it automatically uses the current local time zone** of the host operating system (via C++20 standard zoned_time and current_zone). This method should only be used if you need to manually override the system time zone detection.
  @param secondsOffset The explicit timezone offset in seconds. */
  static void setTimeOffset(int);
  /** @brief Intercepts completed log messages to route them to a custom sink (e.g. file, database, syslog). */
  static void setCompleted(void (*completed)(const Message &, uint8_t)) { data.completed = completed; }
  static void setFunctionPrefixIgnoreList(const std::vector<std::string> &list) { data.functionPrefixIgnoreList_ = list; }
  static void setSenderPrefix(const std::string &prefix) { data.senderPrefix_ = prefix; }

  LogStream(uint8_t, const char *, const char *, int, uint8_t = 0);
  LogStream() = default;
  ~LogStream() noexcept(false);
  /** @brief Generic stream insertion operator for appending variables and primitives. */
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
  LogStream &operator<<(decltype(std::endl<char, std::char_traits<char>>) &);
  LogStream &operator<<(const Color);
  LogStream &operator<<(const int8_t);
  LogStream &operator<<(const uint8_t);
  LogStream &operator<<(const char *);
  LogStream &operator<<(char *);
  template <typename... Args>
  LogStream &output(std::format_string<Args...> msg, Args &&...args) {
    before();
    stream << std::vformat(msg.get(), std::make_format_args(args...));
    after();
    return *this;
  }
  LogStream &output() { return *this; }
  LogStream &output(const std::string &);
  LogStream &space();    ///< Enables automatic spacing between subsequent << insertions.
  LogStream &nospace();  ///< Disables automatic spacing for the rest of this log line.
  LogStream &flush();    ///< Dispatches the current contents to the log output immediately.

private:
  static class Data {
    friend LogStream;

  public:
    static void set(int);

  private:
    Data();
    ~Data();
    int timeOffset = std::numeric_limits<int>::max();
    TimeFormat timeFormat {LOG_STREAM_DEFAULT_TIME_FORMAT, false};
    void (*completed)(const Message &, uint8_t) = &console_output;
    std::vector<std::string> functionPrefixIgnoreList_;
    std::string senderPrefix_;
  } data;

  void before();
  void after();
  std::string name;
  std::stringstream stream;
  uint8_t type;
  const char *file;
  int line;
  uint16_t flags;
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
  #define lsTrace AsyncFw::LogStream(+AsyncFw::LogStream::Trace | AsyncFw::LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsTrace \
    {}            \
    if constexpr (0) AsyncFw::LogStream().output
#endif
#ifndef LS_NO_DEBUG
  #define lsDebug AsyncFw::LogStream(+AsyncFw::LogStream::Debug | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsDebug \
    {}            \
    if constexpr (0) AsyncFw::LogStream().output
#endif
#ifndef LS_NO_INFO
  #define lsInfo AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkYellow, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
  #define lsInfoRed AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
  #define lsInfoGreen AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkGreen, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
  #define lsInfoBlue AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
  #define lsInfoCyan AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkCyan, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
  #define lsInfoMagenta AsyncFw::LogStream(+AsyncFw::LogStream::Info | AsyncFw::LogStream::DarkMagenta, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsInfo \
    {}           \
    if constexpr (0) AsyncFw::LogStream().output
  #define lsInfoRed \
    {}              \
    if constexpr (0) AsyncFw::LogStream().output
  #define lsInfoGreen \
    {}                \
    if constexpr (0) AsyncFw::LogStream().output
  #define lsInfoBlue \
    {}               \
    if constexpr (0) AsyncFw::LogStream().output
  #define lsInfoCyan \
    {}               \
    if constexpr (0) AsyncFw::LogStream().output
  #define lsInfoMagenta \
    {}                  \
    if constexpr (0) AsyncFw::LogStream().output
#endif
#ifndef LS_NO_NOTICE
  #define lsNotice AsyncFw::LogStream(+AsyncFw::LogStream::Notice | AsyncFw::LogStream::Green, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsNotice \
    {}             \
    if constexpr (0) AsyncFw::LogStream().output
#endif
#ifndef LS_NO_WARNING
  #define lsWarning AsyncFw::LogStream(+AsyncFw::LogStream::Warning | AsyncFw::LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsWarning \
    {}              \
    if constexpr (0) AsyncFw::LogStream().output
#endif
#ifndef LS_NO_ERROR
  #define lsError AsyncFw::LogStream(+AsyncFw::LogStream::Error | AsyncFw::LogStream::DarkRed, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS).output
#else
  #define lsError \
    {}            \
    if constexpr (0) AsyncFw::LogStream().output
#endif
