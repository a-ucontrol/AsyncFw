#include <filesystem>
#include <fstream>

#include "core/DataArray.h"
#include "core/LogStream.h"

#include "File.h"

using namespace AsyncFw;

struct File::Private {
  std::fstream f_;
  std::ios::openmode m_;
  std::string fn_;
  std::uintmax_t fs_;
};

File::File(const std::string &fn) {
  private_ = new Private;
  private_->fn_ = fn;
  lsTrace();
}

File::~File() {
  close();
  lsTrace();
  delete private_;
}

bool File::open(const std::string &fn, std::ios::openmode m) {
  private_->fn_ = fn;
  private_->fs_ = std::filesystem::file_size(private_->fn_);
  return open(m);
}

bool File::open(std::ios::openmode m) {
  private_->m_ = m;
  private_->f_.open(std::filesystem::path(private_->fn_), m);
  lsTrace() << private_->fn_;
  return !private_->f_.fail();
}

void File::close() {
  if (private_->f_.is_open()) {
    private_->f_.close();
    lsTrace() << private_->fn_;
    return;
  }
  lsTrace() << LogStream::Color::DarkRed << "not open:" << private_->fn_;
}

void File::flush() {
  if (private_->f_.is_open() && (private_->m_ & std::ios::out)) private_->f_.flush();
}

void File::remove() {
  close();
  std::filesystem::remove(private_->fn_);
}

std::uintmax_t File::size() { return private_->fs_; }

bool File::exists() { return std::filesystem::exists(private_->fn_); }

DataArray File::read(std::size_t s) {
  if (private_->f_.fail()) return {};
  DataArray _da;
  _da.resize((s != SIZE_MAX) ? s : size());
  if (_da.size() == 0) {
    char c;
    while (private_->f_.get(c)) _da.push_back(c);
    return _da;
  }
  std::size_t r = read(reinterpret_cast<char *>(_da.data()), _da.size());
  if (private_->f_.fail() || r <= 0) return {};
  if (r < _da.size()) _da.resize(r);
  return _da;
}

std::streamsize File::read(char *_v, std::streamsize _s) { return private_->f_.readsome(_v, _s); }

std::streamsize File::write(const DataArray &da) { return write(reinterpret_cast<const char *>(da.data()), da.size()); }

std::streamsize File::write(const char *_v, std::streamsize _s) {
  std::fstream::pos_type _p = private_->f_.tellp();
  private_->f_.write(_v, _s);
  if (private_->f_.fail()) return -1;
  return private_->f_.tellp() - _p;
}

std::string File::readLine() {
  std::string s;
  std::getline(private_->f_, s);
  return s;
}

bool File::fail() { return private_->f_.fail(); }

std::fstream &File::fstream() { return private_->f_; }
