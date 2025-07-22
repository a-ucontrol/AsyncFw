#include <filesystem>
#include <fstream>

#include "core/console_msg.hpp"

#include "Rrd.h"

#ifdef EXTEND_RRD_TRACE
  #define trace console_msg
#else
  #define trace(...)
#endif

#define Rrd_COMPRESS_FILE

using namespace AsyncFw;
Rrd::Rrd(int size, const std::string &name) : dbSize(size) {
  trace(__PRETTY_FUNCTION__);
  setName("Rrd");
  start();
  if (size == 0) {
    if (name.empty()) return;
    readOnly = true;
  }
#ifdef AVERAGE_RRD
  setAverage(nullptr, nullptr, 0, 0);
#endif
  last = 0;
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

Rrd::~Rrd() {
  if (running()) {
    quit();
    waitFinished();
  }
  trace(__PRETTY_FUNCTION__);
  if (!file.empty() && !readOnly) { saveToFile(); }
}

bool Rrd::createFile() {
  count_v = 0;
  dataBase.resize(dbSize);
  last = 0;
  if (readOnly || !saveToFile()) return false;
  return true;
}

uint32_t Rrd::append(const Item &data, uint32_t index) {
  mutex.lock();
  if (index == 0) index = last + 1;
  uint32_t pos = index;
#ifdef AVERAGE_RRD
  bool needAverage = (aInterval && (((static_cast<qint64>(index) + aOffset) / aInterval) - ((static_cast<qint64>(last) + aOffset) / aInterval)) > 0) ? true : false;
#endif
  int64_t empty = static_cast<int64_t>(pos) - last;

  if (empty > dbSize) empty = dbSize;
  if (empty < -static_cast<int64_t>(dbSize)) empty = -static_cast<int64_t>(dbSize);

  if (count_v < dbSize && count_v < index) count_v += 1;

  pos %= dbSize;

  if (empty > 0) {
    while (--empty) {
      if (pos == 0) pos = dbSize;
      pos--;
      dataBase[pos].clear();
    }
  } else if (empty < 0) {
    while (empty++) {
      pos++;
      if (pos == dbSize) pos = 0;
      dataBase[pos].clear();
    }
  }
  dataBase[index % dbSize] = data;
  last = index;
  mutex.unlock();
  updated();

#ifdef AVERAGE_RRD
  if (needAverage) {
    if (average_ptr) {
      QList<QByteArray> list;
      read(&list, index - index % aInterval - aInterval, aInterval);
      QByteArray ba = (average_obj->*average_ptr)(list, this);
      emit averaged(ba, (index + aOffset) / aInterval);
    }
  }
#endif
  return index;
}

Rrd::Item Rrd::readFromArray(uint32_t index) {
  uint32_t i = 1 + last + index - count_v;
  //if (i > last) i = last;
  return dataBase[i % dbSize];
}

void Rrd::writeToArray(uint32_t index, const Item &ba) {
  uint32_t i = 1 + last + index - count_v;
  //if (i > last) i = last;
  dataBase[i % dbSize] = ba;
}

void Rrd::clear() {
  std::lock_guard<MutexType> lock(mutex);
  for (uint32_t i = 0; i != dbSize; ++i) dataBase[i].clear();
  last = 0;
  count_v = 0;
}

uint32_t Rrd::lastIndex() {
  std::lock_guard<MutexType> lock(mutex);
  return last;
}

uint32_t Rrd::count() {
  std::lock_guard<MutexType> lock(mutex);
  return count_v;
}

uint32_t Rrd::read(DataArrayList *list, uint32_t val, int size, uint32_t *lastIndex) {
  if (!size) size = dbSize;
  std::lock_guard<MutexType> lock(mutex);
  if (lastIndex) *lastIndex = last;
  if (val > last) return last;
  if ((last - val) > (uint32_t)dbSize) val = 1 + last - dbSize;
  uint32_t i = val % dbSize;
  while (size--) {
    list->push_back(dataBase[i++]);
    if (i == dbSize) i = 0;
    if ((++val) > last) break;
  }
  return val - 1;
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
    ds >> last;
    ds >> dataBase;
    if (ds.fail()) dataBase.clear();
  }

  bool ok = false;
  if (readOnly) {
    dbSize = dataBase.size();
    trace("Rrd: size: " + std::to_string(dbSize));
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
  _ds << last;
  _ds << dataBase;

#ifdef EXTEND_RRD_TRACE
  std::chrono::time_point<std::chrono::steady_clock> t = std::chrono::steady_clock::now();
  int size = _ds.array().size();
#endif

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
    console_msg("Rrd: save failed: " + file);
    return false;
  }
  trace("Rrd: saved: " + fileToSave + ' ' + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count()) + "ms, mem size: " + std::to_string(size) + ", count:" + std::to_string(count_v) + '/' + std::to_string(dbSize) + ", file size:" + std::to_string(_buf.size()) + ", ratio: " + std::to_string(size / static_cast<int>(_buf.size())));
  return true;
}

#ifdef AVERAGE_RRD
void Rrd::setAverage(QObject *obj, QByteArray (QObject::*ptr)(QList<QByteArray> &, Rrd *), int interval, int offset) {
  average_obj = obj;
  average_ptr = ptr;
  aInterval = interval;
  aOffset = offset;
}
#endif

void Rrd::save(const std::string &fn) {
  if (readOnly && fn.empty()) {
    console_msg("Rrd: save error, read only: " + file);
    return;
  }
  if (fn.empty() && file.empty()) {
    console_msg("Rrd: save error, empty file name");
    return;
  }
  if (std::this_thread::get_id() != this->id()) {
    if (running()) {
      invokeMethod([this, fn]() { saveToFile(fn); }, true);
      return;
    }
    console_msg("Rrd: executed from different thread and own thread not running");
  }
  saveToFile(fn);
}
