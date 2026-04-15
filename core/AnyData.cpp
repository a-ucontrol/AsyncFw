/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "AnyData.h"

AsyncFw::AnyData::AnyData(const std::any &data) : data_(data) {}

AsyncFw::AnyData::AnyData() {}

AsyncFw::AnyData::~AnyData() {}
