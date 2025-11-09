#pragma once

#include "core/Thread.h"
#include "core/DataArray.h"

namespace AsyncFw {
class Rrd {
public:
  using Item = DataArray;
  using ItemList = DataArrayList;
  Rrd(int size, int interval, int fillInterval, const std::string &name, AsyncFw::AbstractThread *thread = nullptr);
  Rrd(int size, int interval, int fillInterval, AsyncFw::AbstractThread *thread = nullptr);
  Rrd(int size, const std::string &name, AsyncFw::AbstractThread *thread = nullptr);
  Rrd(int size, AsyncFw::AbstractThread *thread = nullptr);
  ~Rrd();
  uint64_t read(DataArrayList *list, uint64_t from = 0, uint32_t size = 0, uint64_t *lastIndex = nullptr);
  void setAverage(int interval, const std::function<void(const ItemList &)> &f, int offset = 0);
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

  AsyncFw::FunctionConnectorProtected<Rrd>::Connector<> updated;

  AsyncFw::AbstractThread *thread() { return thread_; }

protected:
  AsyncFw::AbstractThread *thread_;
  uint64_t last_;
  uint32_t dbSize;
  ItemList dataBase;
  uint32_t count_v;
  bool readOnly = false;

private:
  bool ownThread = false;
  int aInterval = 0;
  int aOffset = 0;
  int interval;
  int fill;
  std::string file;
  bool createFile();
  bool readFromFile();
  bool saveToFile(const std::string &fn = {});
  std::function<void(const ItemList &)> average;
};
}  // namespace AsyncFw
