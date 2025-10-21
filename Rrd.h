#pragma once

#include "core/Thread.h"
#include "core/DataArray.h"

namespace AsyncFw {
class Rrd {
public:
  using Item = DataArray;
  using ItemList = DataArrayList;
  Rrd(int size, const std::string &name = {});
  ~Rrd();
  uint64_t read(DataArrayList *list, uint64_t from = 0, uint32_t size = 0, uint64_t *lastIndex = nullptr);
  void setAverage(int interval, int offset = 0) {
    aInterval = interval;
    aOffset = offset;
  }
#ifdef AVERAGE_RRD
  void setAverageOffset(int offset) { aOffset = offset; }
  int averageOffset() { return aOffset; }
#endif
  uint32_t size() { return dbSize; }
  uint32_t count();

  //Must lock the thread before calling this method
  Item readFromArray(uint32_t);
  //Must lock the thread before calling this method
  void writeToArray(uint32_t, const Item &);

  uint32_t append(const Item &data, uint64_t index = 0);
  void save(const std::string &fn = {});
  void clear();
  uint64_t lastIndex();

  AsyncFw::FunctionConnectorProtected<Rrd>::Connector<> updated {false};
  AsyncFw::FunctionConnectorProtected<Rrd>::Connector<const ItemList &> average {true};

  AsyncFw::ExecLoopThread *thread() { return thread_; }

protected:
  AsyncFw::ExecLoopThread *thread_;
  uint64_t last_;
  uint32_t dbSize;
  ItemList dataBase;
  uint32_t count_v;
  bool readOnly = false;

private:
  int aInterval = 0;
  int aOffset = 0;
  std::string file;
  bool createFile();
  bool readFromFile();
  bool saveToFile(const std::string &fileToSave = {});
};
}  // namespace AsyncFw
