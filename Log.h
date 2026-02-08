#pragma once

#include <queue>

#include "core/LogStream.h"
#include "Rrd.h"

#define _messages_ 8

namespace AsyncFw {
class AbstractLog {
public:
  using Message = LogStream::Message;

  inline static AbstractLog *instance() { return log_; }
  AbstractLog(bool = false);
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
  static inline std::atomic<AbstractLog *> log_;
  void append(const Message &m);
  virtual void flush();
  virtual void output(const Message &message);
  virtual void save() = 0;
  void process(const Message &);
  void finality();
  void timerTask(int);
  void startTimer(int *, int);
  void stopTimer(int *);
  AbstractThread *thread_;
  uint8_t level = LogStream::Trace;
  int queueLimit = 128;

private:
  struct LastMessage {
    Message message;
    int count = 1;
    int timerId = -1;
    bool marked = false;
  };
  static void append_(const Message &m, uint8_t t);
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
  inline static Log *instance() { return static_cast<Log *>(AbstractLog::instance()); }
  Log(int size, const std::string &name = {}, bool noInstance = false);
  Log(bool noInstance = false) : Log(0, {}, noInstance) {}

  ~Log() override;
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
  virtual void output(const Message &m) override;
};
}  // namespace AsyncFw
