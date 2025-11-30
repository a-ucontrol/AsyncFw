#include "AnyData.h"

#include "LogStream.h"

AsyncFw::AnyData::AnyData(const std::any &data) : data_(data) { lsTrace(); }

AsyncFw::AnyData::AnyData() { lsTrace(); }

AsyncFw::AnyData::~AnyData() { lsTrace(); }
