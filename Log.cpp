#include "core/DataArray.h"
#include "core/console_msg.hpp"

#include "Log.h"

//#define LOG_PREFIX "logger: "
//#define NOTE_PREFIX "note: "

#define LOG_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

#ifdef uC_LOGGER

using namespace AsyncFw;

AbstractLog::AbstractLog(bool noInstance) {
  if (!noInstance && log_ == nullptr) log_ = this;
  LogStream::setCompleted(&append_);
}

AbstractLog::~AbstractLog() {}

void AbstractLog::flush() {
  for (;;) {
    obj_->lock();
    if (messages.empty()) {
      obj_->unlock();
      break;
    }
    Message m = messages.front();
    messages.pop();
    obj_->unlock();
    process(m);
  }
}

void AbstractLog::finality() {
  AbstractLog::flush();
  if (hideDuplicates) {
    for (int i = 0; i != _messages_; ++i) {
      stopTimer(&lastMessages[i].timerId);
      if (lastMessages[i].count > 1 && lastMessages[i].count != 10000) { output({lastMessages[i].message.type, lastMessages[i].message.name, "Repeated message " + std::to_string(lastMessages[i].count) + " times (last: " + std::to_string(LOG_CURRENT_TIME - lastMessages[i].message.time) + "ms ago): " + (lastMessages[i].message.string.empty() ? "Empty message" : lastMessages[i].message.string), lastMessages[i].message.note}); }
    }
  }
  if (log_ == this) log_ = nullptr;
}

void AbstractLog::append(uint8_t type, const std::string &message, const std::string &sender, const std::string &note) { append({type, sender, message, note}); }

void AbstractLog::append(const Message &m) {
  int i = m.type & 0x07;
  if (i > level && i > consoleLevel) return;
  if (!filter.empty()) {
    bool b = false;
    for (const std::string &str : filter) {
      if (m.name == str) {
        b = true;
        break;
      }
    }
    if (!b) return;
  }

  obj_->lock();
  int size = messages.size();
  if (size >= queueLimit) {
    console_msg("AbstractLog::append: many messages in queue");
  #ifndef uC_NO_TRACE
    console_msg(m.string);
  #endif
    obj_->unlock();
    return;
  }
  messages.push(m);
  if (!senderPrefix.empty()) messages.back().name.insert(0, senderPrefix);
  obj_->unlock();
  if (size == 0)
    obj_->invokeMethod([this]() {
      flush();
      if (hideDuplicates) {
        for (int i = 0; i != _messages_; ++i) {
          if (!lastMessages[i].marked) continue;
          lastMessages[i].marked = false;
          startTimer(&lastMessages[i].timerId, 1500);
        }
      }
    });
}

void AbstractLog::process(const Message &m) {
  LastMessage *l = &lastMessages[m.type & 0x07];
  if (hideDuplicates) {
    if (m == l->message) {
      if (l->count < 10000) {
        if (++l->count % 1000 == 0 || l->count == 100 || l->count == 10) {
          if (l->count == 10000) {
            l->marked = false;
            startTimer(&l->timerId, 30000);
            output({m.time, l->message.type, l->message.name, "Repeated message " + std::to_string(l->count) + " or more times: " + (l->message.string.empty() ? "Empty message" : l->message.string), l->message.note});
            return;
          }
          output({m.time, l->message.type, l->message.name, "Repeated message " + std::to_string(l->count) + " times: " + (l->message.string.empty() ? "Empty message" : l->message.string), l->message.note});
        }
      }
      l->marked = true;
      l->message.time = m.time;
      return;
    }
    if (l->count > 1 && l->count <= 10000 && l->count % 1000 != 0 && l->count != 100 && l->count != 10) output({m.time, l->message.type, l->message.name, "Repeated message " + std::to_string(l->count) + " times: " + (l->message.string.empty() ? "Empty message" : l->message.string), l->message.note});
    stopTimer(&l->timerId);
    output(m);
    l->message = m;
    l->count = 1;
    l->marked = true;
    return;
  }
  output(m);
}

void AbstractLog::output(const Message &m) {
  if (obj_->running() && std::this_thread::get_id() != obj_->id()) { console_msg("AbstractLog::output: error: executed from different thread"); }
  if ((m.type & 0x0F) <= consoleLevel) { LogStream::console_output(m, flags | LOG_STREAM_CONSOLE_EXTEND); }
}

void AbstractLog::timerTask(int timerId) {
  if (hideDuplicates) {
    for (int i = 0; i != _messages_; ++i) {
      if (lastMessages[i].timerId != timerId) continue;
      stopTimer(&lastMessages[i].timerId);
      int count = lastMessages[i].count;
      if (count > 1 && count % 1000 && count != 100 && count != 10) output({lastMessages[i].message.type, lastMessages[i].message.name, "Repeated message " + std::to_string(lastMessages[i].count) + " times (last: " + std::to_string(LOG_CURRENT_TIME - lastMessages[i].message.time) + "ms ago): " + (lastMessages[i].message.string.empty() ? "Empty message" : lastMessages[i].message.string), lastMessages[i].message.note});
      lastMessages[i].message = {};
      lastMessages[i].count = 0;
      return;
    }
  }
}

void AbstractLog::startTimer(int *timerId, int msec) {
  if (*timerId >= 0) obj_->removeTimer(*timerId);
  *timerId = obj_->appendTimerTask(msec, [this, timerId]() { timerTask(*timerId); });
}

void AbstractLog::stopTimer(int *timerId) {
  if (*timerId >= 0) {
    obj_->removeTimer(*timerId);
    *timerId = -1;
  }
}

void AbstractLog::append_(const Message &m, uint8_t f) {
  if (log_ && !(f & LOG_STREAM_CONSOLE_ONLY)) log_->append(m);
  else { LogStream::console_output(m, f); }
}

LogMinimal::LogMinimal() {
  obj_ = this;
  start();
  ucTrace();
}

LogMinimal::~LogMinimal() {
  ucTrace();
  quit();
  waitFinished();
}

Log::Log(int size, const std::string &name, bool noInstance) : Rrd(size, name), AbstractLog(noInstance) {
  queueLimit = size / 2;
  obj_ = this;
  ucTrace();
}

Log::~Log() {
  ucTrace();
  quit();
  waitFinished();
}

void Log::finishedEvent() {
  autoSave = -1;
  stopTimer(&timerIdAutosave);
  finality();
}

void Log::output(const Message &m) {
  int i = m.type & 0x07;
  if (i <= level) {
    Rrd::append(rrdItemFromMessage(m));
    if (autoSave > 0) {
      autoSave--;
      if (timerIdAutosave >= 0) modifyTimer(timerIdAutosave, 15000);
      else {
        timerIdAutosave = appendTimerTask(15000, [this]() {
          stopTimer(&timerIdAutosave);
          if (autoSave < 0) return;
          save();
          autoSave = 100;
        });
      }
    }
  }
  AbstractLog::output(m);
}

Rrd::Item Log::rrdItemFromMessage(const Message &m) const {
  DataStream ds;
  ds << m.time << m.type << m.name << m.string << m.note;
  if (!ds.fail()) return ds.array();
  return {};
}

AbstractLog::Message Log::messageFromRrdItem(const Item &_v) const {
  DataStream ds(_v);
  Message m;
  ds >> m.time >> m.type >> m.name >> m.string >> m.note;
  if (!ds.fail()) return m;
  return {};
}
#endif
