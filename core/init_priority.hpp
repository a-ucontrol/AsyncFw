/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/*
MSVC has no __attribute__((init_priority)). We emulate it via
.CRT$XC<letter> sections, which the CRT walks alphabetically.
Letters P..T sit between .CRT$XCL (lib) and .CRT$XCU (user),
leaving XCU (and init_priority 65535 on GCC) as the user-default slot.

Constraints:
  - init_seg applies to all statics declared AFTER it in the TU.
    Use at most once per .cpp (matches current layout).
  - p must be a literal 0..4; ##p does not expand macros/expressions.
  - Ordering is enforced only WITHIN one module (EXE or DLL).
  - p=5 fails on MSVC (undefined section macro) but silently maps to
    user-default on GCC — do not rely on p>=5.
*/

#if defined(_MSC_VER)
  #define ASYNCFW_CRT_SECTION_0 ".CRT$XCP"
  #define ASYNCFW_CRT_SECTION_1 ".CRT$XCQ"
  #define ASYNCFW_CRT_SECTION_2 ".CRT$XCR"
  #define ASYNCFW_CRT_SECTION_3 ".CRT$XCS"
  #define ASYNCFW_CRT_SECTION_4 ".CRT$XCT"
  #define InitPriority(t, v, p) __pragma(section(ASYNCFW_CRT_SECTION_##p, long, read)) __pragma(init_seg(ASYNCFW_CRT_SECTION_##p)) t v
#else
  #define InitPriority(t, v, p) t v __attribute__((init_priority(65530 + p)))
#endif
