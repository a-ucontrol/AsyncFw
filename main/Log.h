/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <queue>

#include "../core/LogStream.h"
#include "Rrd.h"
#include "Instance.h"

#define _messages_ 8

namespace AsyncFw {
/*! \class AbstractLog Log.h <AsyncFw/Log> \brief The AbstractLog class provides the base functionality for logger. */
class AbstractLog {
  friend class Log;

public:
  using Message = LogStream::Message;

  virtual ~AbstractLog() = 0;
  void append(uint8_t, const std::string &, const std::string &, const std::string & = {});
  void setExtendOut(bool b);
  void setColorOut(bool b);
  void setNotesOut(bool b);
  void setHideDuplicates(bool b);
  void setLevel(int i);
  void setConsoleLevel(int i);
  void setFilter(const std::vector<std::string> &f);

protected:
  virtual void output(const Message &message);
  virtual void save() = 0;
  void append(const Message &m);
  void flush();
  void finality();
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
  void startTimer(int *, int);
  void stopTimer(int *);
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

/*! \class Log Log.h <AsyncFw/Log> \brief The Log class.
 \brief Example: \snippet Log/main.cpp snippet */
class Log : public Rrd, public AbstractLog {
public:
  using Message = LogStream::Message;
  using MessageType = LogStream::MessageType;
  using Color = LogStream::Color;

  using AbstractLog::append;

  static inline Log *instance() { return instance_.value; }

  Log(int = 0, const std::string & = {});
  ~Log() override;

  void finality();

  void output(const Message &m) override;

  void setAutoSave(bool b) { autoSave = (b) ? 100 : -1; }
  Rrd::Item rrdItemFromMessage(const Message &m) const;
  Message messageFromRrdItem(const Rrd::Item &_v) const;
  Message readMessage(uint32_t index) { return messageFromRrdItem(readFromArray(index)); }
  void writeMessage(uint32_t index, const Message &message) { writeToArray(index, rrdItemFromMessage(message)); }

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
