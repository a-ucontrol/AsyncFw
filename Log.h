#pragma once

#include <queue>

#include "core/LogStream.h"
#include "Rrd.h"
#include "instance.hpp"

#define _messages_ 8

namespace AsyncFw {
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
  void append(const Message &m);
  virtual void flush();
  virtual void output(const Message &message);
  virtual void save() = 0;
  virtual AbstractThread *thread() = 0;
  void process(const Message &);
  void finality();
  void timerTask(int);
  void startTimer(int *, int);
  void stopTimer(int *);
  uint8_t level = LogStream::Trace;
  int queueLimit = 128;

private:
  struct LastMessage {
    Message message;
    int count = 1;
    int timerId = -1;
    bool marked = false;
  };
  uint8_t flags = LOG_STREAM_CONSOLE_EXTEND
#ifndef _WIN32
                  | LOG_STREAM_CONSOLE_COLOR
#endif
      ;
  uint8_t consoleLevel = LogStream::Trace;
  std::queue<Message> messages;
  LastMessage lastMessages[_messages_];
  bool hideDuplicates = false;
  std::vector<std::string> filter;
};

class Log : public Rrd, public AbstractLog {
public:
  using Message = LogStream::Message;
  using MessageType = LogStream::MessageType;
  using Color = LogStream::Color;

  using AbstractLog::append;

  inline static Log *instance() { return instance_.value; }

  Log(int = 0, const std::string & = {});
  ~Log() override;

  void output(const Message &m) override;

  void setAutoSave(bool b) { autoSave = (b) ? 100 : -1; }
  Rrd::Item rrdItemFromMessage(const Message &m) const;
  Message messageFromRrdItem(const Rrd::Item &_v) const;
  Message readMessage(uint32_t index) { return messageFromRrdItem(readFromArray(index)); }
  void writeMessage(uint32_t index, const Message &message) { writeToArray(index, rrdItemFromMessage(message)); }

  void save() override {
    if (!Rrd::readOnly) Rrd::save();
  }

  AsyncFw::AbstractThread *thread() override { return thread_; }

protected:
  int autoSave;
  int timerIdAutosave = -1;

private:
  inline static struct Instance : public AsyncFw::Instance<Log> {
    void created() override;
  } instance_;
  static void append_(const Message &m, uint8_t t);
};
}  // namespace AsyncFw
