/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#ifdef ENABLE_EXTEND_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
  #define trace_if(x) \
    if (x) LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace() \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
  #define trace_if(x) \
    if constexpr (0) LogStream()
#endif
