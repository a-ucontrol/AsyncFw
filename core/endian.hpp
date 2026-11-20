/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

#ifdef _MSC_VER
  #include <cstdlib>
  #define __builtin_bswap16 _byteswap_ushort
  #define __builtin_bswap32 _byteswap_ulong
  #define __builtin_bswap64 _byteswap_uint64
#endif

namespace AsyncFw::endian {
template <typename T>
constexpr T swap(T v) noexcept {
  static_assert(std::is_trivially_copyable_v<T>);
  if constexpr (sizeof(T) == 1) return v;
  else if constexpr (sizeof(T) == 2) return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(v)));
  else if constexpr (sizeof(T) == 4) return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(v)));
  else if constexpr (sizeof(T) == 8) return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(v)));
  else static_assert(sizeof(T) <= 8, "unsupported size");
}

template <typename T>
constexpr T to_le(T v) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    return std::bit_cast<T>(to_le(std::bit_cast<U>(v)));
  } else if constexpr (std::endian::native == std::endian::big) {
    return swap(v);
  } else {
    return v;
  }
}

template <typename T>
constexpr T from_le(T v) noexcept {
  return to_le(v);
}
}  // namespace AsyncFw::endian
