#include "core/DataArray.h"
#include "core/console_msg.hpp"

#include "Log.h"

#ifdef EXTEND_LOG_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

#define LOG_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

using namespace AsyncFw;

AbstractLog::~AbstractLog() {}

void AbstractLog::flush() {
  Message m;
  for (;;) {
    {  //lock scope
      AbstractThread::LockGuard lock = thread()->lockGuard();
      if (messages.empty()) break;
      m = messages.front();
      messages.pop();
    }
    process(m);
  }
}

void AbstractLog::finality() {
  trace() << thread()->name() << thread()->id();
  AbstractLog::flush();
  if (hideDuplicates) {
    for (int i = 0; i != _messages_; ++i) {
      stopTimer(&lastMessages[i].timerId);
      if (lastMessages[i].count > 1 && lastMessages[i].count != 10000) { output({lastMessages[i].message.type, lastMessages[i].message.name, "Repeated message " + std::to_string(lastMessages[i].count) + " times (last: " + std::to_string(LOG_CURRENT_TIME - lastMessages[i].message.time) + "ms ago): " + (lastMessages[i].message.string.empty() ? "Empty message" : lastMessages[i].message.string), lastMessages[i].message.note}); }
    }
  }
  lsTrace();
}

void AbstractLog::append(uint8_t type, const std::string &message, const std::string &sender, const std::string &note) { append({type, sender, message, note}); }

void AbstractLog::setExtendOut(bool b) {
  thread()->invokeMethod([this, b]() { (b) ? flags |= LOG_STREAM_CONSOLE_EXTEND : flags &= ~LOG_STREAM_CONSOLE_EXTEND; }, true);
}

void AbstractLog::setColorOut(bool b) {
  thread()->invokeMethod([this, b]() { (b) ? flags |= LOG_STREAM_CONSOLE_COLOR : flags &= ~LOG_STREAM_CONSOLE_COLOR; }, true);
}

void AbstractLog::setNotesOut(bool b) {
  thread()->invokeMethod([this, b]() { (b) ? flags |= LOG_STREAM_CONSOLE_LINE : flags &= ~LOG_STREAM_CONSOLE_LINE; }, true);
}

void AbstractLog::setHideDuplicates(bool b) {
  thread()->invokeMethod([this, b]() { hideDuplicates = b; }, true);
}

void AbstractLog::setLevel(int i) {
  thread()->invokeMethod([this, i]() { level = i; }, true);
}

void AbstractLog::setConsoleLevel(int i) {
  thread()->invokeMethod([this, i]() { consoleLevel = i; }, true);
}

void AbstractLog::setFilter(const std::vector<std::string> &f) {
  thread()->invokeMethod([this, f]() { filter = f; }, true);
}

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
  int size;
  {  //lock scope
    AbstractThread::LockGuard lock = thread()->lockGuard();
    size = messages.size();
    if (size >= queueLimit) {
      console_msg("AbstractLog: many messages in queue");
#ifndef LS_NO_TRACE
      console_msg(m.string);
#endif
      return;
    }
    messages.push(m);
  }

  if (size == 0)
    thread()->invokeMethod([this]() {
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
  if (thread()->running() && std::this_thread::get_id() != thread()->id()) { console_msg("AbstractLog: executed from different thread, log thread: " + thread()->name() + ", current thread: " + AbstractThread::currentThread()->name()); }
  if ((m.type & 0x0F) <= consoleLevel) { LogStream::console_output(m, flags); }
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
  if (*timerId >= 0) thread()->removeTimer(*timerId);
  *timerId = thread()->appendTimerTask(msec, [this, timerId]() { timerTask(*timerId); });
}

void AbstractLog::stopTimer(int *timerId) {
  if (*timerId >= 0) {
    thread()->removeTimer(*timerId);
    *timerId = -1;
  }
}

Log::Instance::Instance() { lsTrace(); }

Log::Instance::~Instance() { lsTrace() << LogStream::Color::Magenta << Instance::value(); }

void Log::Instance::created() {
  LogStream::setCompleted(&append_);
  lsTrace() << LogStream::Color::Magenta << Instance::value();
}

Log::Log(int size, const std::string &name) : Rrd(size, name), AbstractLog() {
  autoSave = (size && !name.empty()) ? 100 : -1;
  if (size) queueLimit = size / 2;
  lsTrace();
}

Log::~Log() {
  lsTrace();
  if (Instance::value() == this) Instance::clear();
  if (thread_->running())
    thread_->invokeMethod(
        [this]() {
          stopTimer(&timerIdAutosave);
          autoSave = -1;
          finality();
        },
        true);
  else {
    console_msg("Log thread not running");
    stopTimer(&timerIdAutosave);
    autoSave = -1;
    finality();
  }
}

void Log::output(const Message &m) {
  int i = m.type & 0x07;
  AbstractLog::output(m);
  if (i <= level) {
    Rrd::append(rrdItemFromMessage(m));
    if (autoSave > 0) {
      autoSave--;
      if (timerIdAutosave >= 0) thread_->modifyTimer(timerIdAutosave, 15000);
      else {
        timerIdAutosave = thread_->appendTimerTask(15000, [this]() {
          stopTimer(&timerIdAutosave);
          if (autoSave < 0) return;
          save();
          autoSave = 100;
        });
      }
    }
  }
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

void Log::append_(const Message &m, uint8_t f) {
  AbstractLog *_log = Instance::value();
  if (_log && !(f & LOG_STREAM_CONSOLE_ONLY)) {
    if (f & 0x80) {
      {  //lock scope
        AbstractThread::LockGuard lock = _log->thread()->lockGuard();
        _log->messages.push(m);
      }
      if (!_log->thread()->invokeMethod(
              [_log]() {
                _log->flush();
                _log->save();
              },
              true)) {
        _log->flush();
        _log->save();
      }
      return;
    }
    _log->append(m);
  } else {
    LogStream::console_output(m, f);
  }
}
