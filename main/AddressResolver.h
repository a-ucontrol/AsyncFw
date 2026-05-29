/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file AddressResolver.h @brief The AddressResolver class. */

#include <string>
#include "../core/FunctionConnector.h"
#include "Coroutine.h"

#ifndef _WIN32
  #define AF_UNSPEC_ 0
  #define AF_INET_ 2
  #define AF_INET6_ 10
#else
  #define AF_UNSPEC_ 0
  #define AF_INET_ 2
  #define AF_INET6_ 23
#endif

namespace AsyncFw {
/** @class AddressResolver AddressResolver.h <AsyncFw/AddressResolver> @brief Provides asynchronous DNS resolution using c-ares.
@details AddressResolver encapsulates low-level POSIX address resolution abstractions. It handles asynchronous domain name resolution (DNS lookups) with support for protocol family selection (IPv4 vs IPv6) and custom per-request timeouts.
@brief Examlpe with FunctionConnector: @snippet snippet.dox AddressResolver @brief Examlpe with CoroutineAwait: @snippet snippet.dox AddressResolver coro */
class AddressResolver {
public:
  using Result = std::vector<std::string>;
  enum Family : uint8_t { Unspec = AF_UNSPEC_, Inet = AF_INET_, Inet6 = AF_INET6_ };
  AddressResolver();
  ~AddressResolver();
  /** @brief Starts an asynchronous DNS resolution for the specified hostname. @param name Hostname or domain name to resolve (e.g., "example.com"). @param family Protocol family filter (defaults to IPv4 / Inet). @param timeout Timeout interval for this request in milliseconds (default: 10000 ms).
  \note Triggers the \ref completed connector upon finishing. */
  void resolve(const std::string &, Family = Inet, int timeout = 10000);
  /** @brief The AddressResolver::completed connector. @details Emitted when the DNS resolution completes or times out. @param status Status code of the operation (0 / ARES_SUCCESS on success). @param results Vector of resolved IP address strings. */
  FunctionConnector<int, const std::vector<std::string> &>::Protected<AddressResolver> completed;
  /** @brief Asynchronously resolves the specified hostname (for coroutines). @param name Hostname or domain name to resolve. @param family Protocol family filter (defaults to IPv4 / Inet). @param timeout Timeout interval for this request in milliseconds (default: 10000 ms). @return CoroutineAwait object containing a vector of resolved IP strings.
  @brief Example: \code auto ips = co_await resolver.coResolve("example.com", AddressResolver::Inet); \endcode */
  AsyncFw::CoroutineAwait<Result> coResolve(const std::string &, Family = Inet, int = 10000);

private:
  struct Private;
  struct Private &private_;
};
}  // namespace AsyncFw
