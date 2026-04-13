/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <filesystem>
#include <QFileSystemWatcher>
#include "core/AbstractThread.h"
#include "core/LogStream.h"

#include "main/FileSystemWatcher.h"

using namespace AsyncFw;

#ifdef EXTEND_FILESYSTEMWATCHER_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Black, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::Blue, __PRETTY_FUNCTION__, __FILE__, __LINE__, LS_DEFAULT_FLAGS | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

struct FileSystemWatcher::Private {
  QFileSystemWatcher watcher_;
  struct WatchPath {
    WatchPath() = default;
    WatchPath(const std::string &);
    std::string directory;
    std::string name;
  };
  struct Watch : public WatchPath {
    using WatchPath::WatchPath;
    bool d;
  };
  std::vector<Watch *> files_;
  AbstractThread *thread_;

  int timerid_;
  std::vector<const Watch *> we_;
  void append_(const Watch *);
  void remove_(const Watch *);
  struct CompareWatch {
    bool operator()(const Watch *, const Watch *) const;
    bool operator()(const WatchPath &, const Watch *) const;
    bool operator()(const Watch *, const WatchPath &) const;
  };
};

FileSystemWatcher::Private::WatchPath::WatchPath(const std::string &path) {
  size_t i = path.find_last_of('/');
  if (i == std::string::npos) return;
  directory = path.substr(0, i);
  name = path.substr(i + 1);
}

FileSystemWatcher::FileSystemWatcher(const std::vector<std::string> &paths) {
  private_ = new Private;
  private_->thread_ = AbstractThread::currentThread();
  private_->timerid_ = private_->thread_->appendTimerTask(0, [this]() {
    private_->thread_->modifyTimer(private_->timerid_, 0);
    for (const Private::Watch *f : private_->we_) {
      lsInfoMagenta() << "T" << f->directory + '/' + f->name;
      if (!f->d) {
        notify(f->directory + '/' + f->name, 0);
        return;
      }
      if (std::filesystem::exists(f->directory + '/' + f->name)) {
        notify(f->directory + '/' + f->name, 1);
      } else {
        notify(f->directory + '/' + f->name, -1);
      }
    }
    trace() << LogStream::Color::DarkRed << "timer event" << private_->we_.size();
    private_->we_.clear();
  });

  addPaths(paths);

  QObject::connect(&private_->watcher_, &QFileSystemWatcher::fileChanged, [this](const QString &path) {
    lsInfoMagenta() << "F" << path.toStdString();
    if (!private_->watcher_.files().contains(path)) notify(path.toStdString(), -1);
    Private::WatchPath w(path.toStdString());
    std::vector<Private::Watch *>::iterator it = std::lower_bound(private_->files_.begin(), private_->files_.end(), w, Private::CompareWatch());
    if (it != private_->files_.end() && (*it)->name == w.name && (*it)->directory == w.directory) {
      private_->append_((*it));
      lsInfoMagenta() << "F" << path.toStdString();
    }
  });
  QObject::connect(&private_->watcher_, &QFileSystemWatcher::directoryChanged, [this](const QString &path) {
    lsInfoMagenta() << "D" << path.toStdString();
    Private::WatchPath _w(path.toStdString() + "/*");
    for (const Private::Watch *w : private_->files_) {
      lsInfoMagenta() << "I" << _w.directory << w->directory << w->name << w->d;
      if (w->d && _w.directory == w->directory) {
        private_->append_(w);
        lsInfoMagenta() << "D" << path.toStdString();
      }
    }
  });

  lsTrace();
}

FileSystemWatcher::~FileSystemWatcher() {
  if (instance_.value == this) instance_.value = nullptr;
  private_->thread_->removeTimer(private_->timerid_);
  for (Private::Watch *w : private_->files_) delete w;
  delete private_;
  lsTrace();
}

bool FileSystemWatcher::addPath(const std::string &path) {
  Private::Watch *w = new Private::Watch(path);
  std::vector<Private::Watch *>::iterator it = std::lower_bound(private_->files_.begin(), private_->files_.end(), w, Private::CompareWatch());
  if (it != private_->files_.end() && (*it)->name == w->name && (*it)->directory == w->directory) {
    delete w;
    return false;
  }
  w->d = !(w->name != "*") ? private_->watcher_.addPath(path.c_str()) : true;
  if (w->d) {
    w->d = private_->watcher_.addPath(w->directory.c_str());
    if (!w->d) {
      delete w;
      return false;
    }
  }
  private_->files_.insert(it, w);
  return true;
}

bool FileSystemWatcher::addPaths(const std::vector<std::string> &paths) {
  bool err = false;
  for (const std::string &path : paths)
    if (!addPath(path)) err = true;
  return !err;
}

bool FileSystemWatcher::removePath(const std::string &path) {
  Private::WatchPath wp {path};
  std::vector<Private::Watch *>::iterator itw = std::lower_bound(private_->files_.begin(), private_->files_.end(), wp, Private::CompareWatch());
  if (itw == private_->files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) return false;
  private_->remove_(*itw);
  private_->watcher_.removePath(path.c_str());
  delete *itw;
  private_->files_.erase(itw);
  return true;
}

bool FileSystemWatcher::removePaths(const std::vector<std::string> &paths) {
  bool err = false;
  for (const std::string &path : paths)
    if (!removePath(path)) err = true;
  return !err;
}

std::vector<std::string> FileSystemWatcher::paths() const {
  if (private_->files_.empty()) return {};
  std::vector<std::string> _p;
  for (const Private::Watch *f : private_->files_) { _p.push_back(f->directory + '/' + f->name); }
  return _p;
}

void FileSystemWatcher::Private::append_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  thread_->modifyTimer(timerid_, 1000);
  std::vector<const Watch *>::iterator it = std::find(we_.begin(), we_.end(), _w);
  if (it != we_.end()) return;
  we_.emplace_back(_w);
}

void FileSystemWatcher::Private::remove_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  std::vector<const Watch *>::iterator it = std::find(we_.begin(), we_.end(), _w);
  if (it != we_.end()) we_.erase(it);
}

bool FileSystemWatcher::Private::CompareWatch::operator()(const Watch *w1, const Watch *w2) const {
  int _r = w1->directory.compare(w2->directory);
  if (_r != 0) return (_r < 0);
  return w1->name.compare(w2->name) < 0;
}
bool FileSystemWatcher::Private::CompareWatch::operator()(const WatchPath &p, const Watch *w) const {
  int _r = p.directory.compare(w->directory);
  if (_r != 0 || p.name.empty()) return (_r < 0);
  return p.name.compare(w->name) < 0;
}
bool FileSystemWatcher::Private::CompareWatch::operator()(const Watch *w, const WatchPath &p) const {
  int _r = w->directory.compare(p.directory);
  if (_r != 0 || w->name.empty()) return (_r < 0);
  return w->name.compare(p.name) < 0;
}

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const FileSystemWatcher &w) {
  std::string str = "File list:";
  if (w.private_->files_.empty()) return log << str << "empty";
  for (const std::string &s : w.paths()) { str += '\n' + s; }
  return log << str;
}
}  // namespace AsyncFw
