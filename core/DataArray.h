/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file DataArray.h @brief The DataArray, DataArrayView, DataArrayList and DataStream classes. */

#include <vector>
#include <cstdint>
#include <string>

namespace AsyncFw {
class DataArrayList;
class DataArrayView;
class LogStream;
/** @class DataArray DataArray.h <AsyncFw/DataArray> @brief The DataArray class, provides an array of bytes.
@details Implements a rich set of constructors and conversion tools to seamlessly bridge standard C++ primitives, strings, and raw memory buffers into an encapsulated binary package.
@brief Example: @snippet DataArray/main.cpp snippet */
class DataArray : public std::vector<uint8_t> {
public:
  /** @brief Compresses the provided data view using the framework's default compression algorithm.
  @param view The source binary data view to compress. @return A new compressed DataArray container. */
  static DataArray compress(const DataArrayView &);
  /** @brief Uncompresses the provided compressed data view back to its original raw form.
  @param view The compressed binary data view. @return A new decompressed DataArray container. */
  static DataArray uncompress(const DataArrayView &);
  using std::vector<uint8_t>::vector;
  /** @brief Constructs a byte array from a standard string instance. */
  DataArray(const std::string &);
  /** @brief Constructs a byte array from a null-terminated C-style string. */
  DataArray(const char *);
  /** @brief Constructs a single-byte array containing the provided character. */
  DataArray(const char);
  /** @brief Constructs a byte array by copying raw values from a signed character vector. */
  DataArray(const std::vector<char> &);
  /** @brief Creates a lightweight, non-allocating view slicing a sub-region of this byte array.
  @param offset The starting index of the slice. Defaults to 0. @param length The length of the slice. If 0, captures everything up to the end of the array. @return A read-only DataArrayView mapping the selected range. */
  const DataArrayView view(std::size_t = 0, std::size_t = 0) const;
  /** @brief Appends the contents of a data view directly to the end of this array. */
  DataArray &operator+=(const DataArrayView &);
  /** @brief Concatenates this array and a data view, returning a combined copy. */
  DataArray operator+(const DataArrayView &);
  /** @brief Appends a single character/byte to the end of this array. */
  DataArray &operator+=(const char);
  /** @brief Concatenates this array and a single character/byte, returning a combined copy. */
  DataArray operator+(const char);
};
/** @class DataArrayView DataArray.h <AsyncFw/DataArray> @brief A lightweight, non-allocating string-like descriptor pointing to a contiguous block of binary data.
@details Inherits from std::string_view. Used to pass references to chunks of external memory safely without triggering expensive deep copies.
@brief Example: @snippet DataArray/main.cpp snippet */
class DataArrayView : public std::string_view {
public:
  /** @brief Universal perfect-forwarding constructor proxying arguments directly to the underlying std::string_view. */
  template <typename... Args>
  DataArrayView(Args... args) : std::string_view(args...) {}
  /** @brief Constructs a data view pointing to an explicit raw memory address with a specific size. */
  DataArrayView(const uint8_t *, std::size_t);
  /** @brief Constructs a data view mapping a precise iterator-bounded range of an existing DataArray. */
  DataArrayView(const DataArray::iterator, const DataArray::iterator);
  /** @brief Constructs a data view mapping the entire memory buffer of an existing DataArray. */
  DataArrayView(const DataArray &);
  /** @brief Splices the viewed binary stream into a tokenized list using a specific delimiter character.
  @param delimiter The dividing boundary byte/character. @return A newly constructed list container holding independent slices. */
  DataArrayList split(const char) const;
};
/** @class DataArrayList DataArray.h <AsyncFw/DataArray> @brief A specialized container aggregating multiple DataArray instances.
@brief Example: @snippet DataArray/main.cpp snippet */
class DataArrayList : public std::vector<DataArray> {
public:
  using std::vector<DataArray>::vector;
  /** @brief Flattens and concatenates all elements in this list into a single unified array, separated by a glue character.
  @param delimiter The spacer byte inserted between adjacent items. @return A newly allocation-merged DataArray. */
  DataArray join(const char) const;
};
/** @class DataStream DataArray.h <AsyncFw/DataArray> @brief A fast, compact binary serialization stream.
@details Supports stream operators (<< and >>) to easily pack and unpack primitives, strings, and byte arrays into variable-length compressed blocks.
@note This class explicitly blocks copying and moving.
@brief Example: @snippet DataArray/main.cpp snippet */
class DataStream {
public:
  /** @brief Constructs a write-only serialization stream building an internal data container from scratch. */
  DataStream();
  /** @brief Constructs a read-only deserialization stream operating directly on top of an existing immutable byte array. */
  DataStream(const DataArray &);
  ~DataStream();

  /** @name Serialization Operators
  @{ */
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
  /** @} */

  /** @brief Retrieves a reference to the active cumulative raw byte array built by the stream.
  @return Immutable reference to the binary buffer. */
  const DataArray &array() const;
  /** @brief Checks if the stream pipeline encountered a boundary check failure, bad memory, or malformed data packets.
  @return True if an operation failed or the stream is corrupted. */
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
