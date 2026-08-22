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
  std::fstream f;
  std::ios::openmode m;
  std::string fn;
  std::size_t fs;
};

File::File(const std::string &fn) : private_(*new Private) {
  private_.fn = fn;
  private_.fs = 0;
  lsTrace();
}

File::~File() {
  close();
  lsTrace();
  delete &private_;
}

bool File::open(const std::string &fn, std::ios::openmode m) {
  private_.fn = fn;
  return open(m);
}

bool File::open(std::ios::openmode m) {
  private_.m = m;
  private_.f.open(std::filesystem::path(private_.fn), m);
  if (!private_.f.fail()) {
    std::error_code ec;
    auto _s = std::filesystem::file_size(private_.fn, ec);
    private_.fs = (!ec && _s != static_cast<uintmax_t>(-1)) ? static_cast<std::size_t>(_s) : 0;
  } else private_.fs = 0;
  lsTrace() << private_.fn;
  return !private_.f.fail();
}

bool File::isOpen() { return private_.f.is_open(); }

void File::close() {
  if (private_.f.is_open()) {
    private_.f.close();
    lsTrace() << private_.fn;
    return;
  }
}

void File::flush() {
  if (private_.f.is_open() && (private_.m & std::ios::out)) private_.f.flush();
}

void File::remove() {
  close();
  std::error_code ec;
  std::filesystem::remove(private_.fn, ec);
  private_.fs = 0;
}

std::size_t File::size() { return private_.fs; }

bool File::exists() { return std::filesystem::exists(private_.fn); }

DataArray File::read(std::size_t s) {
  if (private_.f.fail() || !private_.f.is_open()) return {};
  std::size_t v = (private_.fs > static_cast<std::size_t>(tellg())) ? (private_.fs - tellg()) : 0;
  std::size_t _s = s > v ? v : s;
  DataArray _da;
  if (_s > 0) {
    _da.resize(_s);
    std::streamsize r = read(reinterpret_cast<char *>(_da.data()), static_cast<std::streamsize>(_s));
    if (private_.f.fail() || r <= 0) return {};
    return _da;
  }
  // _s == 0 read files in /proc /sys on Linux
  char buf[1024];
  while (true) {
    std::streamsize r = read(buf, sizeof(buf));
    if (r > 0) _da.insert(_da.end(), buf, buf + r);
    else break;
  }
  if (!_da.empty()) private_.f.clear();
  return _da;
}

std::streamsize File::read(char *b, std::streamsize s) {
  private_.f.read(b, s);
  return private_.f.gcount();
}

std::streamsize File::write(const DataArray &da) { return write(reinterpret_cast<const char *>(da.data()), static_cast<std::streamsize>(da.size())); }

std::streamsize File::write(const char *b, std::streamsize s) {
  if (private_.f.fail() || !private_.f.is_open()) return -1;
  std::fstream::pos_type pos = private_.f.tellp();
  private_.f.write(b, s);
  if (private_.f.fail()) return -1;
  std::fstream::pos_type _p = private_.f.tellp();
  if (_p != std::fstream::pos_type(-1) && static_cast<std::size_t>(_p) > private_.fs) private_.fs = static_cast<std::size_t>(_p);
  return _p - pos;
}

std::string File::readLine() {
  std::string s;
  if (!private_.f.is_open() || private_.f.fail()) return s;
  std::getline(private_.f, s);
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

bool File::fail() { return private_.f.fail(); }

std::streamsize File::tellg() { return private_.f.tellg(); }

std::streamsize File::tellp() { return private_.f.tellp(); }

std::fstream &File::fstream() {
  if (private_.m & (std::ios::out | std::ios::app)) private_.fs = std::numeric_limits<std::size_t>::max();
  return private_.f;
}
