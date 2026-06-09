/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Log.h @brief The AbstractLog and Log classes. */

#include <queue>

#include "../core/LogStream.h"
#include "Rrd.h"
#include "Instance.h"

#define _messages_ 8

namespace AsyncFw {
/** @class AbstractLog Log.h <AsyncFw/Log> @brief The AbstractLog class provides the core thread-safe infrastructure for log management and duplicate reduction. */
class AbstractLog {
public:
  using Message = LogStream::Message;

  /** @brief Virtual destructor ensuring safe resource cleanup in derived logging implementations. */
  virtual ~AbstractLog() = 0;
  /** @brief Appends a structured log entry by explicitly passing discrete string components. */
  void append(uint8_t, const std::string &, const std::string &, const std::string & = {});
  /** @brief Toggles extended console output mode containing timestamps and level names. */
  void setExtendOut(bool b);
  /** @brief Toggles ANSI terminal color highlights for outgoing console log sequences. */
  void setColorOut(bool b);
  /** @brief Toggles the display of notes , such as source code lines (file:line), at the end of console entries. */
  void setNotesOut(bool b);
  /** @brief Toggles automated runtime aggregation to compress high-frequency duplicate log messages. */
  void setHideDuplicates(bool b);
  /** @brief Sets the global log severity threshold for file/database recording destinations. */
  void setLevel(int i);
  /** @brief Sets the visibility threshold level exclusively for live stdout/stderr console streaming. */
  void setConsoleLevel(int i);
  /** @brief Enforces an inclusion string filter matching against target sender identification names. */
  void setFilter(const std::vector<std::string> &f);

protected:
  /** @brief Routes an individual message directly to active outputs (like LogStream::console_output). */
  virtual void output(const Message &message);
  /** @brief Abstract interface to persist buffered logs into a specific backend storage. */
  virtual void save() = 0;
  /** @brief Main thread-safe ingestion entry point that pushes a Message structure into the log queue. */
  void append(const Message &m);
  /** @brief Flushes all accumulated messages from the thread-safe queue into the processor loop. */
  void flush();
  /** @brief Synchronously flushes outstanding queues and tears down internal tracking structures. */
  void finality();

  void startTimer(int *, int);
  void stopTimer(int *);
  uint8_t level = LogStream::Trace;
  uint8_t consoleLevel = LogStream::Trace;
  int queueLimit = 128;
  Thread *thread_;

private:
  struct LastMessage {
    Message message;
    int count = 1;
    int timerId = -1;
    bool marked = false;
  };
  void timerTask(int);
  void process(const Message &);
  uint8_t flags = LOG_STREAM_CONSOLE_EXTEND
#ifndef _WIN32
                  | LOG_STREAM_CONSOLE_COLOR
#endif
      ;
  std::queue<Message> messages;
  LastMessage lastMessages[_messages_];
  bool hideDuplicates = false;
  std::vector<std::string> filter;
};

/** @class Log Log.h <AsyncFw/Log> @brief Asynchronous log manager with circular database (Rrd) backend and duplicate сompression.
@details Intercepts engine-wide LogStream invocations, queues them thread-safely, and dispatches writing tasks to a dedicated backend thread.
@brief Example: @snippet Log/main.cpp snippet */
class Log : public Rrd, public AbstractLog {
public:
  using Message = LogStream::Message;
  using MessageType = LogStream::MessageType;
  using Color = LogStream::Color;

  using AbstractLog::append;

  /** @brief Global service locator shortcut to fetch the current engine-wide default log pointer. */
  static inline Log *instance() { return instance_.value; }

  /** @brief Constructs a persistent log system with a specific circular database size and file name context. */
  Log(int = 0, const std::string & = {});
  /** @brief Destructor. Atomically unbinds global log routing hooks and finalizes all active background buffers. */
  ~Log() override;

  /** @brief Performs early shutdown, restoring original console routing. */
  void finality();
  /** @brief Persists log entries into the underlying Rrd buffer and manages automated disk flushing. */
  void output(const Message &m) override;
  /** @brief Controls whether modifications to the log buffer automatically trigger scheduled disk synchronizations. */
  void setAutoSave(bool b) { autoSave = (b) ? 100 : -1; }
  /** @brief Unrolls a Message structure into a flat serialized DataArray binary block suitable for Rrd storage. */
  Rrd::Item rrdItemFromMessage(const Message &m) const;
  /** @brief Parses a serialized Rrd binary DataArray record back into a structured Message instance. */
  Message messageFromRrdItem(const Rrd::Item &_v) const;
  /** @brief Fetches and decodes an archive log message from the circular history database at a given index. */
  Message readMessage(uint32_t index) { return messageFromRrdItem(readFromArray(index)); }
  /** @brief Directly overwrites a circular log archive slot with a freshly serialized log entry structure. */
  void writeMessage(uint32_t index, const Message &message) { writeToArray(index, rrdItemFromMessage(message)); }

  /** @brief Safe write transaction proxy that bypasses filesystem synchronization if the database is in read-only mode. */
  void save() override {
    if (!Rrd::readOnly) Rrd::save();
  }

protected:
  using AbstractLog::thread_;
  int autoSave;
  int timerIdAutosave = -1;

private:
  static class Instance : public AsyncFw::Instance<Log> {
  public:
    using AsyncFw::Instance<Log>::Instance;
    void created() override;
  } instance_;
  static void lsAppend(const Message &m, uint8_t t);
  FunctionConnectionGuard tdg;
};
}  // namespace AsyncFw
