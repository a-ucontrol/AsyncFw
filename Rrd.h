/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include "core/FunctionConnector.h"
#include "core/DataArray.h"

namespace AsyncFw {
class Thread;
/*! \brief The Rrd class. */
class Rrd {
public:
  using Item = DataArray;
  using ItemList = DataArrayList;
  template <typename T>
  void setAverage(int _interval, T f, int _offset = 0) {
    average = new FunctionArgs<const ItemList &>::Function(f);
    aInterval = _interval / interval;
    aOffset = _offset;
  }
  Rrd(int size, int interval, int fillInterval, const std::string &name);
  Rrd(int size, int interval, int fillInterval);
  Rrd(int size, const std::string &name);
  Rrd(int size);
  ~Rrd();
  uint64_t read(DataArrayList *list, uint64_t from = 0, uint32_t size = 0, uint64_t *lastIndex = nullptr);
  void setFillInterval(int interval);
  uint32_t size() { return dbSize; }
  uint32_t count();

  //Must lock the thread before calling this method
  Item readFromArray(uint32_t);
  //Must lock the thread before calling this method
  void writeToArray(uint32_t, const Item &);

  void append(const Item &data, uint64_t index = 0);
  void save(const std::string &fn = {});
  void clear();
  uint64_t lastIndex();

  FunctionConnectorProtected<Rrd>::Connector<> updated;

  Thread *thread() { return thread_; }

protected:
  Thread *thread_;
  uint64_t last_;
  uint32_t dbSize;
  ItemList dataBase;
  uint32_t count_v;
  bool readOnly = false;

private:
  AbstractFunction<const ItemList &> *average = nullptr;
  int aInterval = 0;
  int aOffset = 0;
  int interval;
  uint32_t fill;
  std::string file;
  bool createFile();
  bool readFromFile();
  bool saveToFile(const std::string &fn = {});
};
}  // namespace AsyncFw
