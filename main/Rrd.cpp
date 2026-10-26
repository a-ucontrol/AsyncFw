/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <filesystem>
#include <fstream>

#include "core/LogStream.h"
#include "core/console_msg.hpp"
#include "core/Thread.h"
#include "Rrd.h"

#ifdef EXTEND_RRD_TRACE
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

#define Rrd_CURRENT_TIME (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

#define Rrd_COMPRESS_FILE

using namespace AsyncFw;

Rrd::Rrd(int size, int interval, int fillInterval, const std::string &name) : dbSize(size), interval_(interval), fill(interval ? fillInterval / interval : 0) {
  lsTrace();
  thread_ = Thread::current();
  first_ = 0;
  last_ = 0;

  if (size == 0) readOnly = true;

  if (!name.empty()) {
    file = name;
    if (!readFromFile()) {
      if (std::filesystem::exists(file) || readOnly) console_msg("Rrd", "read failed: " + name);

      if (!createFile() && !readOnly) console_msg("Rrd", "create failed: " + name);
    }
  } else if (!readOnly) dataBase.resize(dbSize);
}
Rrd::Rrd(int size, int interval, int fillInterval) : Rrd(size, interval, fillInterval, {}) {}

Rrd::~Rrd() {
  if (average) delete average;
  if (!file.empty() && !readOnly) { saveToFile(); }
  lsTrace();
}

Rrd::Rrd(int size, const std::string &name) : Rrd(size, 0, 0, name) {}

Rrd::Rrd(int size) : Rrd(size, 0, 0, {}) {}

bool Rrd::createFile() {
  dataBase.clear();
  dataBase.resize(dbSize);
  first_ = 0;
  last_ = 0;
  if (readOnly || !saveToFile()) return false;
  return true;
}

void Rrd::append(const Item &data, uint64_t index) {
  if (!dbSize) return;
  uint64_t pos;
  bool _average;
  {  //lock scope
    AbstractThread::LockGuard lock = thread_->lockGuard();
    if (index) {
      if (!interval_ && index != last_ + 1 && last_) {
        console_msg("Rrd", "explicit non-sequential index in auto-index mode, expected: " + std::to_string(last_ + 1) + " got: " + std::to_string(index));
        return;
      }
      pos = index;
    } else pos = interval_ ? Rrd_CURRENT_TIME / interval_ : last_ + 1;

    uint32_t empty;
    if (pos > last_) {
      uint64_t v = pos - last_;
      if (v > dbSize) v = dbSize;
      empty = v;
    } else {
      trace() << ((pos == last_) ? "index exists:" : "error index:") << index;
      return;
    }

    if (first_ == 0) first_ = pos;
    else if (pos - first_ + 1 > dbSize) first_ = pos - dbSize + 1;

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
    (*average)(list);
  }
}

Rrd::Item Rrd::readFromArray(uint32_t index) {  //Must lock the thread before calling this method
  return dataBase[(first_ + index) % dbSize];
}

void Rrd::writeToArray(uint32_t index, const Item &ba) {  //Must lock the thread before calling this method
  dataBase[(first_ + index) % dbSize] = ba;
}

void Rrd::clear() {
  {  //lock scope
    AbstractThread::LockGuard lock = thread_->lockGuard();
    for (uint32_t i = 0; i != dbSize; ++i) dataBase[i].clear();
    first_ = 0;
    last_ = 0;
  }
  updated();
}

uint64_t Rrd::lastIndex() {
  AbstractThread::LockGuard lock = thread_->lockGuard();
  return last_;
}

uint32_t Rrd::count() const {
  AbstractThread::LockGuard lock = thread_->lockGuard();
  return (first_ == 0) ? 0 : last_ - first_ + 1;
}

uint64_t Rrd::read(DataArrayList *list, uint64_t index, uint32_t size, uint64_t *lastIndex) {
  if (!size) size = dbSize;
  AbstractThread::LockGuard lock = thread_->lockGuard();
  if (lastIndex) *lastIndex = last_;
  if (index > last_) return last_;
  if (index < first_) index = first_;
  uint32_t i = index % dbSize;
  while (size--) {
    list->push_back(dataBase[i++]);
    if (i == dbSize) i = 0;
    if ((++index) > last_) break;
  }
  return index - 1;
}

bool Rrd::readFromFile() {
  std::ifstream _f;
  _f.open(file, std::ios::binary);
  if (_f.fail()) return false;
  _f.seekg(0, std::ios::end);
  std::streamsize _size = _f.tellg();
  _f.seekg(0, std::ios::beg);
  DataArray _buf;
  _buf.resize(_size);
  _f.read(reinterpret_cast<char *>(_buf.data()), _buf.size());
  _f.close();

#ifdef Rrd_COMPRESS_FILE
  _buf = DataArray::uncompress(_buf);
#endif

  if (_buf.empty()) return false;

  DataStream ds(_buf);
  ds >> last_;
  ds >> dataBase;
  if (!ds.end()) ds >> first_;
  else first_ = 0;
  if (ds.fail()) {
    trace() << "(ds.fail())";
    return false;
  }

  if (readOnly) {
    dbSize = dataBase.size();
    trace() << "Rrd: size:" << dbSize;
  }
  if (static_cast<uint32_t>(dataBase.size()) != dbSize) {
    console_msg("Rrd", "error database size: " + std::to_string(dataBase.size()));
    return false;
  }
  /*
  first_ computed by the formula equals the real first_ if and only
  if last_ - first_ + 1 >= dbSize
  (span ≥ dbSize, i.e. the ring has wrapped at least once) or interval_ == 0
  */
  if (first_ == 0) { first_ = (dbSize == 0 || last_ == 0) ? 0 : (last_ > dbSize ? last_ - dbSize + 1 : 1); }

  updated();
  return true;
}

bool Rrd::saveToFile(const std::string &_fileToSave) {
  DataStream _ds;
  _ds << last_;
  _ds << dataBase;
  if (interval_) _ds << first_;

  if (_ds.fail()) {
    trace() << "(ds.fail())";
    return false;
  }

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
    console_msg("Rrd", "save failed: " + fileToSave);
    return false;
  }

#ifdef EXTEND_RRD_TRACE
  std::chrono::time_point<std::chrono::steady_clock> t = std::chrono::steady_clock::now();
  int size = _ds.array().size();
  trace() << "Rrd: saved:" << fileToSave << std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count()) + "ms, mem size: " + std::to_string(size) + ", count:" + std::to_string((first_ == 0) ? 0 : last_ - first_ + 1) + '/' + std::to_string(dbSize) + ", file size:" + std::to_string(_buf.size()) + ", ratio: " + std::to_string(size / static_cast<int>(_buf.size()));
#endif
  return true;
}

void Rrd::save(const std::string &fn) {
  if (readOnly && fn.empty()) {
    console_msg("Rrd", "save error, read only: " + file);
    return;
  }
  if (fn.empty() && file.empty()) {
    console_msg("Rrd", "save error, empty file name");
    return;
  }
  if (std::this_thread::get_id() != thread_->id()) {
    if (thread_->invoke([this, fn]() { saveToFile(fn); }, true)) return;
    console_msg("Rrd", "save executed from different thread and own thread not running");
  }
  saveToFile(fn);
}
