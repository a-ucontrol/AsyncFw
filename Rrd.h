#pragma once

#include "core/Thread.h"
#include "core/DataArray.h"

//#define AVERAGE_RRD //!!!

namespace AsyncFw {
class Rrd : public ExecLoopThread {
public:
  using Item = DataArray;
  using ItemList = DataArrayList;
  Rrd(int size, const std::string &name = {});
  ~Rrd() override;
  uint32_t read(DataArrayList *list, uint32_t from = 0, int size = 0, uint32_t *lastIndex = nullptr);
#ifdef AVERAGE_RRD
  void setAverage(QObject *, QByteArray (QObject::*)(QList<QByteArray> &, Rrd *), int interval, int offset = 0);
  void setAverageOffset(int offset) { aOffset = offset; }
  int averageOffset() { return aOffset; }
#endif
  uint32_t size() { return dbSize; }
  uint32_t count();
  Item readFromArray(uint32_t);
  void writeToArray(uint32_t, const Item &);
  uint32_t append(const Item &data, uint32_t index = 0);
  void save(const std::string &fn = {});
  void clear();
  uint32_t lastIndex();

  void setUpdated(std::function<void()> _updated) { updated_ = _updated; }
  void updated() {
    if (updated_) updated_();
  }

protected:
  uint32_t last;
  uint32_t dbSize;
  ItemList dataBase;
  uint32_t count_v;
  bool readOnly = false;

private:
#ifdef AVERAGE_RRD
  QByteArray (QObject::*average_ptr)(QList<QByteArray> &, Rrd *);
  QObject *average_obj = nullptr;
  int aInterval = 0;
  int aOffset = 0;
#endif
  std::string file;
  bool createFile();
  bool readFromFile();
  bool saveToFile(const std::string &fileToSave = {});
  std::function<void()> updated_;

#ifdef AVERAGE_RRD
signals:
  void averaged(QByteArray, uint32_t);
#endif
};
}  // namespace AsyncFw
