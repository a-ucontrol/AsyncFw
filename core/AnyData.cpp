/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "AnyData.h"

#include "LogStream.h"

AsyncFw::AnyData::AnyData(const std::any &data) : data_(data) { lsTrace(); }

AsyncFw::AnyData::AnyData() { lsTrace(); }

AsyncFw::AnyData::~AnyData() { lsTrace(); }
