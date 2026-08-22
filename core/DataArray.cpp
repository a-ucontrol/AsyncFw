/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <cstring>
#include <zlib.h>
#include "DataArray.h"
#include "LogStream.h"
#include "console_msg.hpp"

#define LOG_DATA_ARAY_SIZE_LIMIT 4096

using namespace AsyncFw;
DataArray DataArray::compress(const DataArrayView &v) {
  std::size_t _size = v.size();
  uLongf _uLongf = compressBound(_size);
  uint8_t j = 0;
  for (;; ++j) {
    if (!(_size >> (5 + j * 8))) break;
  }
  DataArray _c;
  _c.resize(_uLongf + sizeof(uint8_t) + j);
  if (::compress(_c.data() + sizeof(uint8_t) + j, &_uLongf, reinterpret_cast<const uint8_t *>(v.data()), v.size())) {
    console_msg("DataArray", "compress failed");
    return {};
  }
  _c[0] = j | (_size << 3);
  _size >>= 5;
  for (int i = 0; i != j; ++i) _c[sizeof(uint8_t) + i] = (reinterpret_cast<char *>(&_size))[i];
  _c.resize(_uLongf + sizeof(uint8_t) + j);
  return _c;
}

DataArray DataArray::uncompress(const DataArrayView &v) {
  if (v.empty()) return {};
  std::size_t _size = 0;
  uint8_t j = (v[0] & 0x07);
  if (j) {
    if (v.size() < sizeof(uint8_t) + j) return {};
    for (int i = 0; i != j; ++i) (reinterpret_cast<char *>(&_size))[i] = v[sizeof(uint8_t) + i];
    _size <<= 5;
  }
  _size |= (static_cast<uint8_t>(v[0]) >> 3);
  DataArray _u;
  try {
    _u.resize(_size);
  } catch (std::exception &e) {
    console_msg("DataArray", "uncompress failed: " + e.what());
    return {};
  }
  uLongf _uLongf = _size;
  if (::uncompress(_u.data(), &_uLongf, reinterpret_cast<const uint8_t *>(v.data()) + sizeof(uint8_t) + j, v.size() - sizeof(uint8_t) - j)) {
    console_msg("DataArray", "uncompress failed");
    return {};
  }
  if (_size != _uLongf) return {};
  return _u;
}

DataArray::DataArray(const std::string &string) : std::vector<uint8_t>(string.begin(), string.end()) {}

DataArray::DataArray(const char *string) : DataArray(std::string(string)) {}

DataArray::DataArray(const char c) : std::vector<uint8_t>(1, c) {}

DataArray::DataArray(const std::vector<char> &v) : std::vector<uint8_t>(v.begin(), v.end()) {}

const DataArrayView DataArray::view(std::size_t i, std::size_t j) const {
  if (i >= std::vector<uint8_t>::size()) return {};
  if (!j || i + j >= std::vector<uint8_t>::size()) return {data() + i, std::vector<uint8_t>::size() - i};
  return {data() + i, static_cast<std::size_t>(j)};
}

DataArray &DataArray::operator+=(const DataArrayView &v) {
  insert(end(), v.begin(), v.end());
  return *this;
}

DataArray DataArray::operator+(const DataArrayView &v) {
  DataArray _da(*this);
  _da.insert(_da.end(), v.begin(), v.end());
  return _da;
}

DataArray &DataArray::operator+=(const char v) {
  insert(end(), v);
  return *this;
}

DataArray DataArray::operator+(const char v) {
  DataArray _da(*this);
  _da.insert(_da.end(), v);
  return _da;
}

DataArrayView::DataArrayView(const uint8_t *data, std::size_t size) : std::string_view(reinterpret_cast<const char *>(data), size) {}

DataArrayView::DataArrayView(const DataArray::iterator begin, const DataArray::iterator end) : std::string_view(reinterpret_cast<const char *>(&(*begin)), end - begin) {}

DataArrayView::DataArrayView(const DataArray &da) : DataArrayView(da.data(), da.size()) {}

DataArrayList DataArrayView::split(const char c) const {
  if (empty()) return {};
  DataArrayList l;
  int i = 0;
  for (;;) {
    size_t j = find(c, i);
    l.push_back({data() + i, (j == std::string::npos) ? data() + size() : data() + j});
    if (j == std::string::npos) break;
    i = j + 1;
  }
  return l;
}

DataArray DataArrayList::join(const char c) const {
  if (empty()) return {};
  DataArray da = (*this)[0];
  for (std::size_t i = 1; i != size(); ++i) {
    da.push_back(c);
    da += (*this)[i];
  }
  return da;
}

struct DataStream::Private {
  Private(const DataArray *da = nullptr) {
    if (da) {
      read = true;
      data = const_cast<DataArray *>(da);
    } else {
      data = new DataArray;
    }
  }
  template <typename T>
  void rs(T &_v, std::size_t _s) {
    if (_s > data->size() - pos) {
      fail = true;
      return;
    }
    try {
      _v.resize(_s);
    } catch (std::exception &e) { fail = true; };
  }
  void w(int, const uint8_t *);
  void r(int, uint8_t *);
  void sw(std::size_t);
  void sr(std::size_t *);
  DataArray *data;
  bool fail = false;
  bool read = false;
  std::size_t pos = 0;
};

DataStream::DataStream() : private_(*new Private) {}

DataStream::DataStream(const DataArray &data) : private_(*new Private(&data)) {}

DataStream::~DataStream() {
  if (!private_.read) delete private_.data;
  delete &private_;
}

DataStream &DataStream::operator<<(int8_t v) {
  private_.w(sizeof(int8_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int8_t &v) {
  private_.r(sizeof(int8_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

const DataArray &DataStream::array() const { return *private_.data; }

bool DataStream::fail() const { return private_.fail; }

DataStream &DataStream::operator<<(uint8_t v) {
  private_.w(sizeof(uint8_t), &v);
  return *this;
}

DataStream &DataStream::operator>>(uint8_t &v) {
  private_.r(sizeof(uint8_t), &v);
  return *this;
}

DataStream &DataStream::operator<<(int16_t v) {
  private_.w(sizeof(int16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int16_t &v) {
  private_.r(sizeof(int16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint16_t v) {
  private_.w(sizeof(uint16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint16_t &v) {
  private_.r(sizeof(uint16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(int32_t v) {
  private_.w(sizeof(int32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int32_t &v) {
  private_.r(sizeof(int32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint32_t v) {
  private_.w(sizeof(uint32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint32_t &v) {
  private_.r(sizeof(uint32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(int64_t v) {
  private_.w(sizeof(int64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int64_t &v) {
  private_.r(sizeof(int64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint64_t v) {
  private_.w(sizeof(uint64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint64_t &v) {
  private_.r(sizeof(uint64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(float v) {
  private_.w(sizeof(float), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(float &v) {
  private_.r(sizeof(float), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(double v) {
  private_.w(sizeof(double), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(double &v) {
  private_.r(sizeof(double), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(std::string &v) {
  std::size_t _s;
  private_.sr(&_s);
  if (private_.fail) return *this;
  private_.rs(v, _s);
  if (private_.fail) return *this;
  private_.r(_s, reinterpret_cast<uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator<<(const std::string &v) {
  private_.sw(v.size());
  if (private_.fail) return *this;
  private_.w(v.size(), reinterpret_cast<const uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator>>(DataArray &v) {
  std::size_t _s;
  private_.sr(&_s);
  if (private_.fail) return *this;
  private_.rs(v, _s);
  if (private_.fail) return *this;
  private_.r(_s, v.data());
  return *this;
}

DataStream &DataStream::operator<<(const DataArray &v) {
  private_.sw(v.size());
  if (private_.fail) return *this;
  private_.w(v.size(), v.data());
  return *this;
}

DataStream &DataStream::operator<<(const DataArrayView &v) {
  private_.sw(v.size());
  if (private_.fail) return *this;
  private_.w(v.size(), reinterpret_cast<const uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator>>(DataArrayList &v) {
  std::size_t _s;
  private_.sr(&_s);
  if (private_.fail) return *this;
  DataArrayList _l;
  for (std::size_t i = 0; i != _s; ++i) {
    DataArray _d;
    *this >> _d;
    if (private_.fail) return *this;
    _l.push_back(std::move(_d));
  }
  v = std::move(_l);
  return *this;
}

DataStream &DataStream::operator<<(const DataArrayList &v) {
  std::size_t _s = v.size();
  private_.sw(_s);
  if (private_.fail) return *this;
  for (std::size_t i = 0; i != _s; ++i) {
    *this << v[i];
    if (private_.fail) return *this;
  }
  return *this;
}

void DataStream::Private::w(int size, const uint8_t *p) {
  if (fail) return;
  if (read) {
    fail = true;
    console_msg("DataStream", "trying write to stream for read");
    return;
  }
  data->insert(data->end(), p, p + size);
}

void DataStream::Private::r(int size, uint8_t *p) {
  if (fail || !size) return;
  if (!read) {
    fail = true;
    console_msg("DataStream", "trying read from stream for write");
    return;
  }
  if ((pos + size) > data->size()) {
    fail = true;
    return;
  }
  std::memcpy(p, data->data() + pos, size);
  pos += size;
}

void DataStream::Private::sw(std::size_t _s) {
  uint8_t i = 0;
  for (std::size_t _tmp = _s >> 5; _tmp; ++i) _tmp >>= 8;
  uint8_t j = i | (_s << 3);
  w(sizeof(uint8_t), &j);
  if (fail || !i) return;
  _s >>= 5;
  w(i, reinterpret_cast<uint8_t *>(&_s));
}

void DataStream::Private::sr(std::size_t *s) {
  uint8_t i;
  r(sizeof(uint8_t), &i);
  if (fail) return;
  uint8_t j = (i & (sizeof(std::size_t) - 1));  // (i & 0x07) -> sizeof(std::size_t) == 8 , (i & 0x03) -> sizeof(std::size_t) == 4
  i >>= 3;
  if (!j) {
    *s = i;
    return;
  }
  *s = 0;
  r(j, reinterpret_cast<uint8_t *>(s));
  if (fail) return;
  *s <<= 5;
  *s |= i;
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const DataArray &v) { return log << v.view(); }
LogStream &operator<<(LogStream &log, const DataArrayView &v) {
  if (v.empty()) {
    log << "[]";
    return log;
  }
  if (v.size() > LOG_DATA_ARAY_SIZE_LIMIT) {
    log << "[Large data, size: " + std::to_string(v.size()) + "]";
    return log;
  }
  for (const std::basic_string_view<char>::value_type &c : v)
    if (!std::isprint(c) && c != '\n' && c != '\r' && c != '\t') {
      log << "[Binary data, size: " + std::to_string(v.size()) + "]";
      return log;
    }
  log << static_cast<std::string_view>(v);
  return log;
}

LogStream &operator<<(LogStream &log, const DataArrayList &v) {
  if (v.empty()) {
    log << "[[]]";
    return log;
  }
  log << "[[Items: " + std::to_string(v.size()) + "]]";
  return log;
}

LogStream &operator<<(LogStream &log, const DataStream &v) { return (log << v.array()); }
}  // namespace AsyncFw
