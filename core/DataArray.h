/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*! \file DataArray.h \brief The DataArray, DataArrayView, DataArrayList and DataStream classes. */

#include <vector>
#include <cstdint>
#include <string>

namespace AsyncFw {
class DataArrayList;
class DataArrayView;
class LogStream;

/*! \class DataArray DataArray.h <AsyncFw/DataArray> \brief The DataArray class, provides an array of bytes.
\brief Example: \snippet DataArray/main.cpp snippet */
class DataArray : public std::vector<uint8_t> {
public:
  static DataArray compress(const DataArrayView &);
  static DataArray uncompress(const DataArrayView &);
  using std::vector<uint8_t>::vector;
  DataArray(const std::string &);
  DataArray(const char *);
  DataArray(const char);
  DataArray(const std::vector<char> &);
  const DataArrayView view(std::size_t = 0, std::size_t = 0) const;
  DataArray &operator+=(const DataArrayView &);
  DataArray operator+(const DataArrayView &);
  DataArray &operator+=(const char);
  DataArray operator+(const char);
};

/*! \class DataArrayView DataArray.h <AsyncFw/DataArray> \brief The DataArrayView class.
\brief Example: \snippet DataArray/main.cpp snippet */
class DataArrayView : public std::string_view {
public:
  template <typename... Args>
  DataArrayView(Args... args) : std::string_view(args...) {}
  DataArrayView(const uint8_t *, std::size_t);
  DataArrayView(const DataArray::iterator, const DataArray::iterator);
  DataArrayView(const DataArray &);
  DataArrayList split(const char) const;
};

/*! \class DataArrayList DataArray.h <AsyncFw/DataArray> \brief The DataArrayList class.
\brief Example: \snippet DataArray/main.cpp snippet */
class DataArrayList : public std::vector<DataArray> {
public:
  using std::vector<DataArray>::vector;
  DataArray join(const char) const;
};

/*! \class DataStream DataArray.h <AsyncFw/DataArray> \brief The DataStream class.
\brief Example: \snippet DataArray/main.cpp snippet */
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
  const DataArray &array() const;
  bool fail() const;

private:
  struct Private;
  Private &private_;
};
LogStream &operator<<(LogStream &, const DataArray &);
LogStream &operator<<(LogStream &, const DataArrayView &);
LogStream &operator<<(LogStream &, const DataArrayList &);
LogStream &operator<<(LogStream &, const DataStream &);
}  // namespace AsyncFw
