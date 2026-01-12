#pragma once

#include <ios>
#include <cstdint>

namespace AsyncFw {
class DataArray;
class File {
  struct Private;

public:
  File(const std::string & = {});
  ~File();
  bool open(const std::string &, std::ios::openmode = std::ios::binary | std::ios::in);
  bool open(std::ios::openmode = std::ios::binary | std::ios::in);
  void close();
  void flush();
  void remove();
  std::uintmax_t size();
  bool exists();
  DataArray read(std::size_t = SIZE_MAX);
  std::streamsize write(const DataArray &);
  std::streamsize read(char *, std::streamsize);
  std::streamsize write(const char *, std::streamsize);
  std::string readLine();
  bool fail();

  std::fstream &fstream();

private:
  File(File &) = delete;
  File &operator=(File &) = delete;
  Private *private_;
};
}  // namespace AsyncFw
