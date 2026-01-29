#include <filesystem>
#include <fstream>

#include "core/LogStream.h"
#include "Rrd.h"

#include "core/console_msg.hpp"

#ifdef EXTEND_RRD_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_LOG_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
#else
  #define trace(x) \
    if constexpr (0) LogStream()
#endif

#define Rrd_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

#define Rrd_COMPRESS_FILE

using namespace AsyncFw;

Rrd::Rrd(int size, int interval, int fillInterval, const std::string &name) : dbSize(size), interval(interval), fill(interval ? fillInterval / interval : 0) {
  lsTrace();
  thread_ = AbstractThread::currentThread();
  if (size == 0) {
    if (name.empty()) return;
    readOnly = true;
  }
  last_ = 0;
  if (!name.empty()) {
    file = name;
    if (!readFromFile()) {
      if (std::filesystem::exists(file) || readOnly) console_msg("Rrd: read failed: " + name);

      if (!createFile() && !readOnly) console_msg("Rrd: create failed: " + name);
    }
  } else {
    count_v = 0;
    dataBase.resize(dbSize);
  }
}
Rrd::Rrd(int size, int interval, int fillInterval) : Rrd(size, interval, fillInterval, {}) {}

Rrd::~Rrd() {
  if (!file.empty() && !readOnly) { saveToFile(); }
  lsTrace();
}

Rrd::Rrd(int size, const std::string &name) : Rrd(size, 0, 0, name) {}

Rrd::Rrd(int size) : Rrd(size, 0, 0, {}) {}

bool Rrd::createFile() {
  count_v = 0;
  dataBase.clear();
  dataBase.resize(dbSize);
  last_ = 0;
  if (readOnly || !saveToFile()) return false;
  return true;
}

void Rrd::append(const Item &data, uint64_t index) {
  uint64_t pos = index ? index : interval ? Rrd_CURRENT_TIME / interval : 1;
  bool _average;
  {  //lock scope
    AbstractThread::LockGuard lock = thread_->lockGuard();
    if (!index && !interval) pos += last_;

    uint32_t empty;
    if (pos > last_) {
      uint64_t v = pos - last_;
      if (v > dbSize) v = dbSize;
      empty = v;
    } else {
      trace() << ((pos == last_) ? "index exists:" : "error index:") << index;
      return;
    }

    if (count_v < dbSize && count_v < pos) count_v += 1;
    uint32_t i = pos % dbSize;
    dataBase[i] = data;

    if (empty > 1) {
      bool _fill = empty <= fill;
      while (--empty) {
        if (i == 0) i = dbSize;
        i--;
        if (!_fill) dataBase[i].clear();
        else { dataBase[i] = data; }
      }
    }

    _average = (aInterval && (((pos + aOffset) / aInterval) - ((last_ + aOffset) / aInterval)) > 0);

    last_ = pos;
  }
  updated();

  if (_average) {
    ItemList list;
    read(&list, pos - pos % aInterval - aInterval, aInterval);
    average(list);
  }
}

Rrd::Item Rrd::readFromArray(uint32_t index) {  //Must lock the thread before calling this method
  uint64_t i = 1 + last_ + index - count_v;
  return dataBase[i % dbSize];
}

void Rrd::writeToArray(uint32_t index, const Item &ba) {  //Must lock the thread before calling this method
  uint64_t i = 1 + last_ + index - count_v;
  dataBase[i % dbSize] = ba;
}

void Rrd::clear() {
  {  //lock scope
    AbstractThread::LockGuard lock = thread_->lockGuard();
    for (uint32_t i = 0; i != dbSize; ++i) dataBase[i].clear();
    last_ = 0;
    count_v = 0;
  }
  updated();
}

uint64_t Rrd::lastIndex() {
  AbstractThread::LockGuard lock = thread_->lockGuard();
  return last_;
}

uint32_t Rrd::count() {
  AbstractThread::LockGuard lock = thread_->lockGuard();
  return count_v;
}

uint64_t Rrd::read(DataArrayList *list, uint64_t val, uint32_t size, uint64_t *lastIndex) {
  if (!size) size = dbSize;
  AbstractThread::LockGuard lock = thread_->lockGuard();
  if (lastIndex) *lastIndex = last_;
  if (val > last_) return last_;
  if ((last_ - val) > dbSize) val = 1 + last_ - dbSize;
  uint32_t i = val % dbSize;
  while (size--) {
    list->push_back(dataBase[i++]);
    if (i == dbSize) i = 0;
    if ((++val) > last_) break;
  }
  return val - 1;
}

void Rrd::setAverage(int _interval, const std::function<void(const ItemList &)> &_f, int _offset) {
  average = _f;
  aInterval = _interval / interval;
  aOffset = _offset;
}

bool Rrd::readFromFile() {
  std::ifstream _f;
  _f.open(file, std::ios::binary);
  if (_f.fail()) return false;
  _f.seekg(0, std::ios::end);
  uint32_t _size = _f.tellg();
  _f.seekg(0, std::ios::beg);
  DataArray _buf;
  _buf.resize(_size);
  _f.read(reinterpret_cast<char *>(_buf.data()), _buf.size());
  _f.close();

#ifdef Rrd_COMPRESS_FILE
  _buf = DataArray::uncompress(_buf);
#endif

  if (!_buf.empty()) {
    DataStream ds(_buf);
    ds >> last_;
    ds >> dataBase;
    if (ds.fail()) dataBase.clear();
  }

  bool ok = false;
  if (readOnly) {
    dbSize = dataBase.size();
    trace() << "Rrd: size:" << dbSize;
  }
  if (static_cast<uint32_t>(dataBase.size()) != dbSize) {
    console_msg("Rrd: error database size: " + std::to_string(dataBase.size()));
  } else {
    ok = true;
    count_v = 0;
    for (const Item &ba : dataBase)
      if (!ba.empty()) count_v++;
    updated();
  }
  return ok;
}

bool Rrd::saveToFile(const std::string &_fileToSave) {
  DataStream _ds;
  _ds << last_;
  _ds << dataBase;

#ifdef Rrd_COMPRESS_FILE
  DataArray _buf = DataArray::compress(_ds.array());
  if (_buf.empty()) return false;
#else
  #define _buf _ds.array()
#endif

  std::ofstream _f;
  std::string fileToSave = (!_fileToSave.empty()) ? _fileToSave : file;
  _f.open(fileToSave, std::ios::binary);
  if (!_f.fail()) _f.write(reinterpret_cast<const char *>(_buf.data()), _buf.size());
  if (!_f.fail()) _f.flush();
  _f.close();

  if (_f.fail()) {
    console_msg("Rrd: save failed: " + fileToSave);
    return false;
  }

#ifdef EXTEND_RRD_TRACE
  std::chrono::time_point<std::chrono::steady_clock> t = std::chrono::steady_clock::now();
  int size = _ds.array().size();
  trace() << "Rrd: saved:" << fileToSave << std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count()) + "ms, mem size: " + std::to_string(size) + ", count:" + std::to_string(count_v) + '/' + std::to_string(dbSize) + ", file size:" + std::to_string(_buf.size()) + ", ratio: " + std::to_string(size / static_cast<int>(_buf.size()));
#endif
  return true;
}

void Rrd::save(const std::string &fn) {
  if (readOnly && fn.empty()) {
    console_msg("Rrd: save error, read only: " + file);
    return;
  }
  if (fn.empty() && file.empty()) {
    console_msg("Rrd: save error, empty file name");
    return;
  }
  if (std::this_thread::get_id() != thread_->id()) {
    if (thread_->running()) {
      thread_->invokeMethod([this, fn]() { saveToFile(fn); }, true);
      return;
    }
    console_msg("Rrd: executed from different thread and own thread not running");
  }
  saveToFile(fn);
}
