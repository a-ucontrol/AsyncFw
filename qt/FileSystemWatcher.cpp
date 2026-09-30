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
  #define ENABLE_EXTEND_TRACE
#endif
#include "core/extend_trace.hpp"

// QFileSystemWatcher emits fileChanged() once per flush on Linux and often
// twice per save on Windows. Without IN_CLOSE_WRITE (which the Qt API does
// not expose) silence is used as a proxy for "end of activity", so bursts
// are aggregated into a single debounced Changed.
#define FILESYSTEMWATCHER_DEBOUNCE_MS 200

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
    bool d;  // false: file watch is active; true: file is missing, rely on directory watch
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

Instance<FileSystemWatcher> FileSystemWatcher::instance_ {"FileSystemWatcher"};

FileSystemWatcher::FileSystemWatcher(const std::vector<std::string> &paths) : private_(*new Private) {
  private_.thread_ = AbstractThread::current();
  private_.timerid_ = private_.thread_->appendTimerTask(0, [this]() {
    private_.thread_->modifyTimer(private_.timerid_, 0);
    for (const Private::Watch *f : private_.we_) { notify(f->directory + '/' + f->name, Changed); }
    trace() << LogStream::Color::DarkRed << "timer event fired, processed:" << private_.we_.size();
    private_.we_.clear();
  });
  addPaths(paths);

  QObject::connect(&private_.watcher_, &QFileSystemWatcher::fileChanged, [this](const QString &path) {
    std::string _path = path.toStdString();
    std::replace(_path.begin(), _path.end(), '\\', '/');
    Private::WatchPath w(_path);
    auto it = std::lower_bound(private_.files_.begin(), private_.files_.end(), w, Private::CompareWatch());
    if (it == private_.files_.end() || (*it)->name != w.name || (*it)->directory != w.directory) return;
    Private::Watch *watchItem = *it;
    if (!private_.watcher_.files().contains(path)) {
      // File removed or renamed. Notify immediately, drop any pending debounced
      // Changed for this file, and switch to the parent directory so a later
      // recreation is detected.
      notify(_path, Removed);
      private_.remove_(watchItem);
      if (!watchItem->d) {
        private_.watcher_.addPath(QString::fromStdString(watchItem->directory));
        watchItem->d = true;
      }
    } else {
      // File modified. Aggregate bursts into a single debounced Changed.
      private_.append_(watchItem);
    }
  });
  QObject::connect(&private_.watcher_, &QFileSystemWatcher::directoryChanged, [this](const QString &path) {
    std::string dirPath = path.toStdString();
    std::replace(dirPath.begin(), dirPath.end(), '\\', '/');
    for (Private::Watch *w : private_.files_) {
      if (w->directory != dirPath || !w->d) continue;
      std::string filePath = w->directory + '/' + w->name;
      if (std::filesystem::exists(filePath) && !private_.watcher_.files().contains(QString::fromStdString(filePath))) {
        private_.watcher_.addPath(QString::fromStdString(filePath));
        w->d = false;
        notify(filePath, Created);
      }
    }
  });
  lsTrace();
}

FileSystemWatcher::~FileSystemWatcher() {
  if (instance_.value == this) instance_.value = nullptr;
  private_.thread_->removeTimer(private_.timerid_);
  for (Private::Watch *w : private_.files_) delete w;
  delete &private_;
  lsTrace();
}

bool FileSystemWatcher::addPath(const std::string &path) {
  Private::Watch *w = new Private::Watch(path);
  if (w->name == "*") {
    delete w;
    lsError() << "Wildcard paths like '*' are supported on Linux implementation only.";
    return false;
  }
  std::vector<Private::Watch *>::iterator it = std::lower_bound(private_.files_.begin(), private_.files_.end(), w, Private::CompareWatch());
  if (it != private_.files_.end() && (*it)->name == w->name && (*it)->directory == w->directory) {
    delete w;
    return false;
  }
  if (private_.watcher_.addPath(path.c_str())) {
    // File exists and is now being watched directly.
    w->d = false;
  } else {
    // File does not exist: fall back to watching the parent directory. The
    // addPath() return value is intentionally ignored — the directory may
    // already be watched on behalf of another pending file in the same dir.
    private_.watcher_.addPath(w->directory.c_str());
    w->d = true;
  }
  private_.files_.insert(it, w);
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
  std::vector<Private::Watch *>::iterator itw = std::lower_bound(private_.files_.begin(), private_.files_.end(), wp, Private::CompareWatch());
  if (itw == private_.files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) return false;
  Private::Watch *w = *itw;
  private_.remove_(w);
  if (w->d) {
    // Directory-watch mode: drop the directory from QFileSystemWatcher only
    // if no other tracked file in the same directory still relies on it.
    bool stillNeeded = false;
    for (const Private::Watch *other : private_.files_) {
      if (other != w && other->d && other->directory == w->directory) {
        stillNeeded = true;
        break;
      }
    }
    if (!stillNeeded) private_.watcher_.removePath(QString::fromStdString(w->directory));
  } else {
    private_.watcher_.removePath(QString::fromStdString(w->directory + '/' + w->name));
  }
  delete w;
  private_.files_.erase(itw);
  return true;
}

bool FileSystemWatcher::removePaths(const std::vector<std::string> &paths) {
  bool err = false;
  for (const std::string &path : paths)
    if (!removePath(path)) err = true;
  return !err;
}

std::vector<std::string> FileSystemWatcher::paths() const {
  if (private_.files_.empty()) return {};
  std::vector<std::string> _p;
  for (const Private::Watch *f : private_.files_) { _p.push_back(f->directory + '/' + f->name); }
  return _p;
}

void FileSystemWatcher::Private::append_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  thread_->modifyTimer(timerid_, FILESYSTEMWATCHER_DEBOUNCE_MS);
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
  if (w.private_.files_.empty()) return log << str << "empty";
  for (const std::string &s : w.paths()) { str += '\n' + s; }
  return log << str;
}
}  // namespace AsyncFw
