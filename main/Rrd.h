/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file Rrd.h @brief The Rrd class. */

#include "../core/FunctionConnector.h"
#include "../core/DataArray.h"

namespace AsyncFw {
class Thread;
/** @class Rrd Rrd.h <AsyncFw/Rrd> @brief A high-performance circular database engine (Round-Robin Database) for time-series and structural data metrics logging.
@details Manages a fixed-size ring buffer array in memory and handles automated filesystem persistence. Supports calculation of averaged metrics and data slicing via historical indexes. */
class Rrd {
public:
  using Item = DataArray;
  using ItemList = DataArrayList;
  /** @brief Sets up an automated callback to compute averaged metrics inside a specified interval fraction.
  @warning This is a strict configuration method. It must be called only once during initialization. */
  template <typename F>
  void setAverage(int interval, F f, int offset = 0) {
    average = new Invocable<void(const ItemList &)>::Function(std::forward<F>(f));
    aInterval = interval / interval_;
    aOffset = offset;
  }
  /** @brief Constructs a persistent circular archive database with physical disk backing. @param size Total number of data allocation slots (capacity) in the ring buffer. @param interval Data collection rate and timeline step resolution in milliseconds. @param fillInterval Maximum allowed time window in milliseconds to auto-fill missed historical data gaps. @param name Path to a file in the local file system used to save data. */
  Rrd(int, int, int, const std::string &);
  /** @brief Constructs an in-memory anonymous circular database without file serialization. @param size Total number of data allocation slots (capacity) in the ring buffer. @param interval Data collection rate and timeline step resolution in milliseconds. @param fillInterval Maximum allowed time window in milliseconds to auto-fill missed historical data gaps. */
  Rrd(int, int, int);
  /** @brief Constructs a persistent circular archive database using default system tracking intervals. @param size Total number of data allocation slots (capacity) in the ring buffer. @param name Path to a file in the local file system used to save data. */
  Rrd(int, const std::string &);
  /** @brief Constructs a minimal in-memory anonymous circular database with default system tracking intervals. @param size Total number of data allocation slots (capacity) in the ring buffer. */
  Rrd(int);
  /** @brief Destructor. Synchronizes outstanding dirty data buffers to disk and frees internal evaluation assets. */
  ~Rrd();

  /** @brief Slices historical time-series data records from a given timestamp and extracts them into a provided destination list container. @return The unique timestamp boundary configuration of the last successfully extracted record item. */
  uint64_t read(DataArrayList *, uint64_t = 0, uint32_t = 0, uint64_t * = nullptr);
  /** @brief Returns the absolute maximum capability size (total slots) configured for this circular ring buffer layout. */
  uint32_t size() { return dbSize; }
  /** @brief Returns the total number of valid data items currently populated inside the database allocation framework. */
  uint32_t count();

  /** @brief Directly extracts a raw binary data array chunk element from the specified ring buffer index allocation slot.
  @warning Must lock the target processing background thread before executing this method context. */
  Item readFromArray(uint32_t);
  /** @brief Directly overwrites a raw binary data array chunk element at the specified ring buffer index allocation slot.
  @warning Must lock the target processing background thread before executing this method context. */
  void writeToArray(uint32_t, const Item &);

  /** @brief Ingests and commits a new item layout entry directly into the tail of the circular timeline framework tracking stack. */
  void append(const Item &data, uint64_t index = 0);
  /** @brief Forces a synchronous flushing execution write path to save memory tables down to an explicit local filesystem file path. */
  void save(const std::string &fn = {});
  /** @brief Wipes out all active records in memory and resets internal buffer ring tracker structures to zero initialization state. */
  void clear();
  /** @brief Returns the absolute unique incremental primary master timeline database record tracking index offset identifier. */
  uint64_t lastIndex();

  /** @brief Protected notification event connector emitted instantly after a data append or modifying transaction successfully updates the data layout. */
  FunctionConnector<>::Protected<Rrd> updated;

  /** @brief Fetches a raw tracking pointer leading back to the dedicated background thread context managing this execution scope. */
  Thread *thread() { return thread_; }

protected:
  Thread *thread_;
  uint64_t last_;
  uint32_t dbSize;
  ItemList dataBase;
  uint32_t count_;
  bool readOnly = false;

private:
  Invocable<void(const ItemList &)>::Abstract *average = nullptr;
  int aInterval = 0;
  int aOffset = 0;
  int interval_;
  uint32_t fill;
  std::string file;
  bool createFile();
  bool readFromFile();
  bool saveToFile(const std::string &fn = {});
};
}  // namespace AsyncFw
