#include <cstring>
#include <zlib.h>
#include "DataArray.h"

#include "console_msg.hpp"

using namespace AsyncFw;
DataArray DataArray::compress(const DataArrayView &_u) {
  std::size_t _size = _u.size();
  uLongf _uLongf = compressBound(_size);
  uint8_t j = 0;
  for (;; ++j) {
    if (!(_size >> (5 + j * 8))) break;
  }
  DataArray _c;
  _c.resize(_uLongf + sizeof(uint8_t) + j);
  if (::compress(_c.data() + sizeof(uint8_t) + j, &_uLongf, reinterpret_cast<const uint8_t *>(_u.data()), _u.size())) {
    console_msg("DataArray: compress failed");
    return {};
  }
  _c[0] = j | (_size << 3);
  _size >>= 5;
  for (int i = 0; i != j; ++i) _c[sizeof(uint8_t) + i] = (reinterpret_cast<char *>(&_size))[i];
  _c.resize(_uLongf + sizeof(uint8_t) + j);
  return _c;
}

DataArray DataArray::uncompress(const DataArrayView &_c) {
  if (_c.empty()) return {};
  std::size_t _size = 0;
  uint8_t j = (_c[0] & 0x07);
  if (j) {
    if (_c.size() < sizeof(uint8_t) + j) return {};
    for (int i = 0; i != j; ++i) (reinterpret_cast<char *>(&_size))[i] = _c[sizeof(uint8_t) + i];
    _size <<= 5;
  }
  _size |= (static_cast<uint8_t>(_c[0]) >> 3);
  DataArray _u;
  try {
    _u.resize(_size);
  } catch (std::exception &e) {
    console_msg("DataArray: uncompress failed: " + e.what());
    return {};
  }
  uLongf _uLongf = _size;
  if (::uncompress(_u.data(), &_uLongf, reinterpret_cast<const uint8_t *>(_c.data()) + sizeof(uint8_t) + j, _c.size() - sizeof(uint8_t) - j)) {
    console_msg("DataArray: uncompress failed");
    return {};
  }
  if (_size != _uLongf) return {};
  return _u;
}

DataArray::DataArray(const std::string &string) : std::vector<uint8_t>(string.begin(), string.end()) {}

DataArray::DataArray(const char *string) : DataArray(std::string(string)) {}

DataArray::DataArray(const char c) : std::vector<uint8_t>(1, c) {}

DataArray::DataArray(const std::vector<char> &v) : std::vector<uint8_t>(v.begin(), v.end()) {}

const DataArrayView DataArray::view(int i, int j) const {
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

DataStream::DataStream() { data_ = new DataArray(); }

DataStream::DataStream(const DataArray &data) : data_(const_cast<DataArray *>(&data)) { read_ = true; }

DataStream::~DataStream() {
  if (!read_) delete data_;
}

DataStream &DataStream::operator<<(int8_t v) {
  w_(sizeof(int8_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int8_t &v) {
  r_(sizeof(int8_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint8_t v) {
  w_(sizeof(uint8_t), &v);
  return *this;
}

DataStream &DataStream::operator>>(uint8_t &v) {
  r_(sizeof(uint8_t), &v);
  return *this;
}

DataStream &DataStream::operator<<(int16_t v) {
  w_(sizeof(int16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int16_t &v) {
  r_(sizeof(int16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint16_t v) {
  w_(sizeof(uint16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint16_t &v) {
  r_(sizeof(uint16_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(int32_t v) {
  w_(sizeof(int32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int32_t &v) {
  r_(sizeof(int32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint32_t v) {
  w_(sizeof(uint32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint32_t &v) {
  r_(sizeof(uint32_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(int64_t v) {
  w_(sizeof(int64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(int64_t &v) {
  r_(sizeof(int64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(uint64_t v) {
  w_(sizeof(uint64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(uint64_t &v) {
  r_(sizeof(uint64_t), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(float v) {
  w_(sizeof(float), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(float &v) {
  r_(sizeof(float), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator<<(double v) {
  w_(sizeof(double), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(double &v) {
  r_(sizeof(double), reinterpret_cast<uint8_t *>(&v));
  return *this;
}

DataStream &DataStream::operator>>(std::string &v) {
  std::size_t _s;
  sr_(&_s);
  if (fail_) return *this;
  rs_(v, _s);
  if (fail_) return *this;
  r_(_s, reinterpret_cast<uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator<<(const std::string &v) {
  sw_(v.size());
  if (fail_) return *this;
  w_(v.size(), reinterpret_cast<const uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator>>(DataArray &v) {
  std::size_t _s;
  sr_(&_s);
  if (fail_) return *this;
  rs_(v, _s);
  if (fail_) return *this;
  r_(_s, v.data());
  return *this;
}

DataStream &DataStream::operator<<(const DataArray &v) {
  sw_(v.size());
  if (fail_) return *this;
  w_(v.size(), v.data());
  return *this;
}

DataStream &DataStream::operator<<(const DataArrayView &v) {
  sw_(v.size());
  if (fail_) return *this;
  w_(v.size(), reinterpret_cast<const uint8_t *>(v.data()));
  return *this;
}

DataStream &DataStream::operator>>(DataArrayList &v) {
  std::size_t _s;
  sr_(&_s);
  if (fail_) return *this;
  rs_(v, _s);
  if (fail_) return *this;
  for (std::size_t i = 0; i != _s; ++i) {
    if (fail_) return *this;
    *this >> v[i];
  }
  return *this;
}

DataStream &DataStream::operator<<(const DataArrayList &v) {
  std::size_t _s = v.size();
  sw_(_s);
  if (fail_) return *this;
  for (std::size_t i = 0; i != _s; ++i) {
    if (fail_) return *this;
    *this << v[i];
  }
  return *this;
}

void DataStream::w_(int size, const uint8_t *p) {
  if (fail_) return;
  if (read_) {
    fail_ = true;
    console_msg("DataStream: trying write to stream for read");
    return;
  }
  data_->insert(data_->end(), p, p + size);
}

void DataStream::r_(int size, uint8_t *p) {
  if (fail_) return;
  if (!read_) {
    fail_ = true;
    console_msg("DataStream: trying read from stream for write");
    return;
  }
  if ((pos_ + size) > data_->size()) {
    fail_ = true;
    return;
  }
  std::memcpy(p, data_->data() + pos_, size);
  pos_ += size;
}

void DataStream::sw_(std::size_t _s) {
  uint8_t i = 0;
  for (;; ++i) {
    if (!(_s >> (5 + i * 8))) break;
  }
  uint8_t j = i | (_s << 3);
  w_(sizeof(uint8_t), &j);
  if (fail_ || !i) return;
  _s >>= 5;
  w_(i, reinterpret_cast<uint8_t *>(&_s));
}

void DataStream::sr_(std::size_t *s) {
  uint8_t i;
  r_(sizeof(uint8_t), &i);
  if (fail_) return;
  uint8_t j = (i & 0x07);
  i >>= 3;
  if (!j) {
    *s = i;
    return;
  }
  *s = 0;
  r_(j, reinterpret_cast<uint8_t *>(s));
  if (fail_) return;
  *s <<= 5;
  *s |= i;
}
