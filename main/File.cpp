/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

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
  std::size_t fs_;
};

File::File(const std::string &fn) : private_(*new Private) {
  private_.fn_ = fn;
  private_.fs_ = 0;
  lsTrace();
}

File::~File() {
  close();
  lsTrace();
  delete &private_;
}

bool File::open(const std::string &fn, std::ios::openmode m) {
  private_.fn_ = fn;
  return open(m);
}

bool File::open(std::ios::openmode m) {
  private_.m_ = m;
  private_.f_.open(std::filesystem::path(private_.fn_), m);
  if (!private_.f_.fail()) {
    std::error_code ec;
    auto _s = std::filesystem::file_size(private_.fn_, ec);
    private_.fs_ = (!ec && _s != static_cast<uintmax_t>(-1)) ? static_cast<std::size_t>(_s) : 0;
  } else private_.fs_ = 0;
  lsTrace() << private_.fn_;
  return !private_.f_.fail();
}

bool File::isOpen() { return private_.f_.is_open(); }

void File::close() {
  if (private_.f_.is_open()) {
    private_.f_.close();
    lsTrace() << private_.fn_;
    return;
  }
}

void File::flush() {
  if (private_.f_.is_open() && (private_.m_ & std::ios::out)) private_.f_.flush();
}

void File::remove() {
  close();
  std::error_code ec;
  std::filesystem::remove(private_.fn_, ec);
  private_.fs_ = 0;
}

std::size_t File::size() { return private_.fs_; }

bool File::exists() { return std::filesystem::exists(private_.fn_); }

DataArray File::read(std::size_t s) {
  if (private_.f_.fail() || !private_.f_.is_open()) return {};
  std::size_t _s = s;
  if (_s == std::numeric_limits<std::size_t>::max()) {
    std::fstream::pos_type _p = private_.f_.tellg();
    if (_p != std::fstream::pos_type(-1) && private_.fs_ > static_cast<std::size_t>(_p)) {
      _s = private_.fs_ - static_cast<std::size_t>(_p);
    } else _s = 0;
  }
  DataArray _da;
  if (_s > 0) {
    _da.resize(_s);
    std::streamsize r = read(reinterpret_cast<char *>(_da.data()), static_cast<std::streamsize>(_s));
    if (private_.f_.fail() || r <= 0) return {};
    if (static_cast<std::size_t>(r) < _da.size()) { _da.resize(static_cast<std::size_t>(r)); }
    return _da;
  }
  char buf[1024];
  while (true) {
    std::streamsize r = read(buf, sizeof(buf));
    if (r > 0) _da.insert(_da.end(), buf, buf + r);
    else break;
  }
  return _da;
}

std::streamsize File::read(char *b, std::streamsize s) {
  private_.f_.read(b, s);
  return private_.f_.gcount();
}

std::streamsize File::write(const DataArray &da) { return write(reinterpret_cast<const char *>(da.data()), static_cast<std::streamsize>(da.size())); }

std::streamsize File::write(const char *b, std::streamsize s) {
  if (private_.f_.fail() || !private_.f_.is_open()) return -1;
  std::fstream::pos_type pos = private_.f_.tellp();
  private_.f_.write(b, s);
  if (private_.f_.fail()) return -1;
  std::fstream::pos_type _p = private_.f_.tellp();
  if (_p != std::fstream::pos_type(-1) && static_cast<std::size_t>(_p) > private_.fs_) private_.fs_ = static_cast<std::size_t>(_p);
  return _p - pos;
}

std::string File::readLine() {
  std::string s;
  if (!private_.f_.is_open() || private_.f_.fail()) return s;
  std::getline(private_.f_, s);
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

bool File::fail() { return private_.f_.fail(); }

std::streamsize File::tellg() { return private_.f_.tellg(); }

std::streamsize File::tellp() { return private_.f_.tellp(); }

std::fstream &File::fstream() {
  if (private_.m_ & (std::ios::out | std::ios::app)) private_.fs_ = std::numeric_limits<std::size_t>::max();
  return private_.f_;
}
