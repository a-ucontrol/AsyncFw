/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include "AnyData.h"
#include "LogStream.h"

AsyncFw::AnyData::AnyData(const std::any &data) : data_(data) { lsTrace(); }

AsyncFw::AnyData::~AnyData() { lsTrace(); }

std::any &AsyncFw::AnyData::data() const { return data_; }

void AsyncFw::AnyData::setData(const std::any &data) const { data_ = data; }

bool AsyncFw::AnyData::hasValue() const { return data_.has_value(); }
