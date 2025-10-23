#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace AsyncFw {
class DataArrayList;
class DataArrayView;

class DataArray : public std::vector<uint8_t> {
public:
  static DataArray compress(const DataArrayView &);
  static DataArray uncompress(const DataArrayView &);
  using std::vector<uint8_t>::vector;
  DataArray(const std::string &);
  DataArray(const char *);
  DataArray(const char);
  DataArray(const std::vector<char> &);
  const DataArrayView view(int = 0, int = 0) const;
  DataArray &operator+=(const DataArrayView &);
  DataArray operator+(const DataArrayView &);
  DataArray &operator+=(const char);
  DataArray operator+(const char);
};

class DataArrayView : public std::string_view {
public:
  DataArrayView(const uint8_t *, std::size_t);
  DataArrayView(const DataArray::iterator, const DataArray::iterator);
  DataArrayView(const DataArray &);
  template <typename... Args>
  DataArrayView(Args... args) : std::string_view(args...) {}
  DataArrayList split(const char) const;
};

class DataArrayList : public std::vector<DataArray> {
public:
  using std::vector<DataArray>::vector;
  DataArray join(const char) const;
};

class DataStream {
public:
  DataStream();
  DataStream(const DataArray &);
  DataStream(const DataStream &) = delete;
  ~DataStream();
  DataStream &operator<<(int8_t);
  DataStream &operator>>(int8_t &);
  DataStream &operator<<(uint8_t);
  DataStream &operator>>(uint8_t &);
  DataStream &operator<<(int16_t);
  DataStream &operator>>(int16_t &);
  DataStream &operator<<(uint16_t);
  DataStream &operator>>(uint16_t &);
  DataStream &operator<<(int32_t);
  DataStream &operator>>(int32_t &);
  DataStream &operator<<(uint32_t);
  DataStream &operator>>(uint32_t &);
  DataStream &operator<<(int64_t);
  DataStream &operator>>(int64_t &);
  DataStream &operator<<(uint64_t);
  DataStream &operator>>(uint64_t &);
  DataStream &operator<<(float);
  DataStream &operator>>(float &);
  DataStream &operator<<(double);
  DataStream &operator>>(double &);
  DataStream &operator<<(const std::string &);
  DataStream &operator>>(std::string &);
  DataStream &operator<<(const DataArrayView &);
  DataStream &operator<<(const DataArray &);
  DataStream &operator>>(DataArray &);
  DataStream &operator<<(const DataArrayList &);
  DataStream &operator>>(DataArrayList &);
  const DataArray &array() const { return *data_; }
  bool fail() const { return fail_; }

private:
  template <typename T>
  void rs_(T &_v, std::size_t _s) {
    if (read_ && _s >= data_->size()) {
      fail_ = true;
      return;
    }
    try {
      _v.resize(_s);
    } catch (std::exception &e) {
      fail_ = true;
      error(e.what());
    };
  }
  void error(const std::string &);
  void w_(int, const uint8_t *);
  void r_(int, uint8_t *);
  void sw_(std::size_t);
  void sr_(std::size_t *);
  DataArray *data_;
  bool fail_ = false;
  bool read_ = false;
  std::size_t pos_ = 0;
};
}  // namespace AsyncFw
