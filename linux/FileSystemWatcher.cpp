/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#include <sys/ioctl.h>
#include <sys/inotify.h>
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
  struct WatchPath {
    WatchPath() = default;
    WatchPath(const std::string &);
    std::string directory;
    std::string name;
  };
  struct Watch : public WatchPath {
    using WatchPath::WatchPath;
    int d;
  };
  std::vector<Watch *> files_;
  std::vector<Watch *> wds_;
  int notifyfd_;
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
  struct CompareWatchDescriptor {
    bool operator()(const Watch *, const Watch *) const;
    bool operator()(const Watch *, int) const;
  };
};

FileSystemWatcher::Private::WatchPath::WatchPath(const std::string &path) {
  size_t i = path.find_last_of('/');
  if (i == std::string::npos) return;
  directory = path.substr(0, i);
  name = path.substr(i + 1);
}

Instance<FileSystemWatcher> FileSystemWatcher::instance_ {"FileSystemWatcher"};

FileSystemWatcher::FileSystemWatcher(const std::vector<std::string> &paths) {
  private_ = new Private;
  private_->thread_ = AbstractThread::currentThread();
  private_->timerid_ = private_->thread_->appendTimerTask(0, [this]() {
    private_->thread_->modifyTimer(private_->timerid_, 0);
    for (const Private::Watch *f : private_->we_) notify(f->directory + '/' + f->name, 0);
    trace() << LogStream::Color::DarkRed << "timer event" << private_->we_.size();
    private_->we_.clear();
  });

  private_->notifyfd_ = inotify_init();

  private_->thread_->appendPollTask(private_->notifyfd_, AbstractThread::PollIn, [this](AbstractThread::PollEvents) {
    int size;
    if (ioctl(private_->notifyfd_, FIONREAD, &size) != 0) {
      lsError();
      return;
    }
    char *buf = new char[size];
    if (read(private_->notifyfd_, buf, size) != size) {
      lsError();
      return;
    }
    int offset = 0;
    while (offset < size) {
      const struct inotify_event *e = reinterpret_cast<const struct inotify_event *>(buf + offset);
      offset += sizeof(inotify_event) + e->len;

      std::vector<Private::Watch *>::iterator itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), e->wd, Private::CompareWatchDescriptor());
      if (itd == private_->wds_.end() || (*itd)->d != e->wd) {
        trace() << LogStream::Color::Red << "itd == wds_.end() || (*itd)->d != e->wd";
        continue;
      }

      if (!(*itd)->name.empty()) {
        if (e->mask & (IN_IGNORED | IN_DELETE_SELF)) {
          Private::Watch *w = *itd;
          private_->wds_.erase(itd);
          int i = inotify_add_watch(private_->notifyfd_, w->directory.c_str(), IN_CREATE | IN_DELETE);
          inotify_rm_watch(private_->notifyfd_, w->d);
          private_->remove_(w);
          notify(w->directory + '/' + w->name, -1);
          lsDebug() << "removed" << w->directory + '/' + w->name << w->d;

          w->d = inotify_add_watch(private_->notifyfd_, (w->directory + '/' + w->name).c_str(), IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF);
          if (w->d >= 0) {
            inotify_rm_watch(private_->notifyfd_, i);
            notify(w->directory + '/' + w->name, 1);
            lsDebug() << "created (event missed)" << w->directory + '/' + w->name << w->d;
            itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), w->d, Private::CompareWatchDescriptor());
            private_->wds_.insert(itd, w);
            continue;
          }

          if (i < 0) continue;

          Private::Watch *dw = new Private::Watch();
          dw->d = i;
          dw->directory = w->directory;
          itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), dw->d, Private::CompareWatchDescriptor());
          private_->wds_.insert(itd, dw);
          continue;
        }
        if (e->mask & IN_CLOSE_WRITE) {
          notify((*itd)->directory + '/' + (*itd)->name, 0);
          private_->remove_(*itd);
          lsDebug() << "close write" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        if (e->mask & IN_MODIFY) {
          private_->append_(*itd);
          lsDebug() << "modify" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        if (e->mask & IN_ATTRIB) {
          //notify((*itd)->directory + '/' + (*itd)->name, 0);
          //private_->remove_(*itd);
          private_->append_(*itd);
          lsDebug() << "attributes changed" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        continue;
      }

      Private::WatchPath wp;
      wp.directory = (*itd)->directory;
      wp.name = e->name;
      std::vector<Private::Watch *>::iterator itw = std::lower_bound(private_->files_.begin(), private_->files_.end(), wp, Private::CompareWatch());
      if (itw == private_->files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) {
        wp.name = "*";
        itw = std::lower_bound(private_->files_.begin(), private_->files_.end(), wp, Private::CompareWatch());
        if (itw == private_->files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) continue;
      }
      if (e->mask & IN_CREATE) {
        if ((*itw)->name == e->name) {
          (*itw)->d = inotify_add_watch(private_->notifyfd_, (wp.directory + '/' + e->name).c_str(), IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF);
          if ((*itw)->d < 0) {
            lsWarning() << "inotify add watch error:" << (*itw)->d;
            (*itw)->d = (*itd)->d;
          } else {
            itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), (*itw)->d, Private::CompareWatchDescriptor());
            private_->wds_.insert(itd, *itw);
          }
        }
        notify(wp.directory + '/' + e->name, 1);
        lsDebug() << "created (watch directory)" << wp.directory + '/' + wp.name;
        continue;
      }
      if (e->mask & IN_DELETE) {
        if ((*itw)->d == (*itd)->d) {
          notify(wp.directory + '/' + e->name, -1);
          lsDebug() << "removed" << wp.directory + '/' + wp.name;
        }
        continue;
      }

      trace() << LogStream::Color::DarkRed << "unknown event" << wp.directory + '/' + wp.name << (*itd)->d << ((e->len) ? e->name : "") << e->wd << e->mask;
    }
    delete[] buf;
  });

  addPaths(paths);
  lsTrace();
}

FileSystemWatcher::~FileSystemWatcher() {
  if (instance_.value == this) instance_.value = nullptr;
  private_->thread_->removePollDescriptor(private_->notifyfd_);
  private_->thread_->removeTimer(private_->timerid_);
  ::close(private_->notifyfd_);

  for (Private::Watch *w : private_->wds_)
    if (w->name.empty()) delete w;
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

  w->d = (w->name != "*") ? inotify_add_watch(private_->notifyfd_, path.c_str(), IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF) : -1;
  if (w->d < 0) {
    int i = inotify_add_watch(private_->notifyfd_, w->directory.c_str(), IN_CREATE | IN_DELETE);
    if (i < 0) {
      delete w;
      return false;
    }
    w->d = i;
    Private::Watch *dw = new Private::Watch();
    dw->d = i;
    dw->directory = w->directory;
    std::vector<Private::Watch *>::iterator itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), dw->d, Private::CompareWatchDescriptor());
    if (itd == private_->wds_.end() || (*itd)->d != dw->d) {
      private_->wds_.insert(itd, dw);
    } else {
      delete dw;
    }
  } else {
    std::vector<Private::Watch *>::iterator itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), w->d, Private::CompareWatchDescriptor());
    private_->wds_.insert(itd, w);
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
  std::vector<Private::Watch *>::iterator itd = std::lower_bound(private_->wds_.begin(), private_->wds_.end(), (*itw)->d, Private::CompareWatchDescriptor());
  warning_if(itd == wds_.end() || (*itd)->d != (*itw)->d) << LogStream::Color::Red << "itd == wds_.end() || (*itd)->d != (*itw)->d";
  inotify_rm_watch(private_->notifyfd_, (*itd)->d);
  if (!(*itd)->name.empty()) private_->wds_.erase(itd);
  else {
    Private::WatchPath wp;
    wp.directory = (*itd)->directory;
    std::pair<std::vector<Private::Watch *>::iterator, std::vector<Private::Watch *>::iterator> itp = std::equal_range(private_->files_.begin(), private_->files_.end(), wp, Private::CompareWatch());
    if (itp.second - itp.first == 1) {
      inotify_rm_watch(private_->notifyfd_, (*itp.first)->d);
      trace() << LogStream::Color::DarkRed << "remove directory watch" << wp.directory << (*itp.first)->d;
      delete *itd;
      private_->wds_.erase(itd);
    }
  }
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
  std::vector<const Private::Watch *>::iterator it = std::lower_bound(we_.begin(), we_.end(), _w, Private::CompareWatch());
  if (it != we_.end() && *it == _w) return;
  we_.insert(it, _w);
}

void FileSystemWatcher::Private::remove_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  std::vector<const Private::Watch *>::iterator it = std::lower_bound(we_.begin(), we_.end(), _w, Private::CompareWatch());
  if (it != we_.end() && *it == _w) we_.erase(it);
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

bool FileSystemWatcher::Private::CompareWatchDescriptor::operator()(const Watch *d1, const Watch *d2) const { return d1->d < d2->d; }
bool FileSystemWatcher::Private::CompareWatchDescriptor::operator()(const Watch *d, int fd) const { return d->d < fd; }

namespace AsyncFw {
LogStream &operator<<(LogStream &log, const FileSystemWatcher &w) {
  std::string str = "File list:";
  if (w.private_->files_.empty()) return log << str << "empty";
  for (const std::string &s : w.paths()) { str += '\n' + s; }
  return log << str;
}
}  // namespace AsyncFw
