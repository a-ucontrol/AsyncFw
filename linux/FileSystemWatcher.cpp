#include <sys/ioctl.h>
#include <sys/inotify.h>
#include "core/Thread.h"
#include "core/LogStream.h"

#include "FileSystemWatcher.h"

using namespace AsyncFw;

#ifdef EXTEND_FILESYSTEMWATCHER_TRACE
  #define trace LogStream(+LogStream::Trace | LogStream::Gray, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output
  #define warning_if(x) \
    if (x) LogStream(+LogStream::Warning | LogStream::DarkBlue, __PRETTY_FUNCTION__, __FILE__, __LINE__, 6 | LOG_STREAM_CONSOLE_ONLY).output()
#else
  #define trace(x) \
    if constexpr (0) LogStream()
  #define warning_if(x) \
    if constexpr (0) LogStream()
#endif

FileSystemWatcher::WatchPath::WatchPath(const std::string &path) {
  size_t i = path.find_last_of('/');
  if (i == std::string::npos) return;
  directory = path.substr(0, i);
  name = path.substr(i + 1);
}

FileSystemWatcher::FileSystemWatcher(const std::vector<std::string> &paths) {
  fileSystemWatcher = this;
  thread_ = AbstractThread::currentThread();
  timerid_ = thread_->appendTimerTask(0, [this]() {
    thread_->modifyTimer(timerid_, 0);
    for (const Watch *f : we_) watch(f->directory + '/' + f->name, 0);
    trace() << LogStream::Color::DarkRed << "timer event" << we_.size();
    we_.clear();
  });

  notifyfd_ = inotify_init();

  thread_->appendPollTask(notifyfd_, AbstractThread::PollIn, [this](AbstractThread::PollEvents) {
    int size;
    if (ioctl(notifyfd_, FIONREAD, &size) != 0) {
      ucError();
      return;
    }
    char *buf = new char[size];
    if (read(notifyfd_, buf, size) != size) {
      ucError();
      return;
    }
    int offset = 0;
    while (offset < size) {
      const struct inotify_event *e = reinterpret_cast<const struct inotify_event *>(buf + offset);
      offset += sizeof(inotify_event) + e->len;

      std::vector<Watch *>::iterator itd = std::lower_bound(wds_.begin(), wds_.end(), e->wd, CompareWatchDescriptor());
      if (itd == wds_.end() || (*itd)->d != e->wd) {
        trace() << LogStream::Color::Red << "itd == wds_.end() || (*itd)->d != e->wd";
        continue;
      }

      if (!(*itd)->name.empty()) {
        if (e->mask & (IN_IGNORED | IN_DELETE_SELF)) {
          inotify_rm_watch(notifyfd_, (*itd)->d);
          watch((*itd)->directory + '/' + (*itd)->name, -1);
          remove_(*itd);
          ucDebug() << "removed" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          (*itd)->d = -1;
          wds_.erase(itd);
          continue;
        }
        if (e->mask & IN_CLOSE_WRITE) {
          watch((*itd)->directory + '/' + (*itd)->name, 0);
          remove_(*itd);
          ucDebug() << "close write" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        if (e->mask & IN_MODIFY) {
          append_(*itd);
          ucDebug() << "modify" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        if (e->mask & IN_ATTRIB) {
          watch((*itd)->directory + '/' + (*itd)->name, 0);
          remove_(*itd);
          //append_(*itd);
          ucDebug() << "attributes changed" << (*itd)->directory + '/' + (*itd)->name << (*itd)->d;
          continue;
        }
        continue;
      }

      WatchPath wp;
      wp.directory = (*itd)->directory;
      wp.name = e->name;
      std::vector<Watch *>::iterator itw = std::lower_bound(files_.begin(), files_.end(), wp, CompareWatch());
      if (itw == files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) {
        wp.name = "*";
        itw = std::lower_bound(files_.begin(), files_.end(), wp, CompareWatch());
        if (itw == files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) continue;
      }
      if (e->mask & IN_CREATE) {
        if ((*itw)->name == e->name) {
          (*itw)->d = inotify_add_watch(notifyfd_, (wp.directory + '/' + e->name).c_str(), IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF);
          if ((*itw)->d < 0) {
            logWarning() << "Inotify add watch error:" << (*itw)->d;
            (*itw)->d = (*itd)->d;
          } else {
            itd = std::lower_bound(wds_.begin(), wds_.end(), (*itw)->d, CompareWatchDescriptor());
            wds_.insert(itd, *itw);
          }
        }
        watch(wp.directory + '/' + e->name, 1);
        ucDebug() << "created" << wp.directory + '/' + wp.name;
        continue;
      }
      if (e->mask & IN_DELETE) {
        if ((*itw)->d == (*itd)->d) {
          watch(wp.directory + '/' + e->name, -1);
          ucDebug() << "removed" << wp.directory + '/' + wp.name;
        }
        continue;
      }

      trace() << LogStream::Color::DarkRed << "unknown event" << wp.directory + '/' + wp.name << (*itd)->d << ((e->len) ? e->name : "") << e->wd << e->mask;
    }
    delete[] buf;
  });

  addPaths(paths);
  ucTrace();
}

FileSystemWatcher::~FileSystemWatcher() {
  thread_->removePollDescriptor(notifyfd_);
  thread_->removeTimer(timerid_);
  ::close(notifyfd_);

  for (Watch *w : wds_)
    if (w->name.empty()) delete w;
  for (Watch *w : files_) delete w;
  ucTrace();
}

bool FileSystemWatcher::addPath(const std::string &path) {
  Watch *w = new Watch(path);
  std::vector<Watch *>::iterator it = std::lower_bound(files_.begin(), files_.end(), w, CompareWatch());
  if (it != files_.end() && (*it)->name == w->name && (*it)->directory == w->directory) {
    delete w;
    return false;
  }

  w->d = (w->name != "*") ? inotify_add_watch(notifyfd_, path.c_str(), IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF) : -1;
  if (w->d < 0) {
    int i = inotify_add_watch(notifyfd_, w->directory.c_str(), IN_CREATE | IN_DELETE);
    if (i < 0) {
      delete w;
      return false;
    }
    w->d = i;
    Watch *dw = new Watch();
    dw->d = i;
    dw->directory = w->directory;
    std::vector<Watch *>::iterator itd = std::lower_bound(wds_.begin(), wds_.end(), dw->d, CompareWatchDescriptor());
    if (itd == wds_.end() || (*itd)->d != dw->d) {
      wds_.insert(itd, dw);
    } else {
      delete dw;
    }
  } else {
    std::vector<Watch *>::iterator itd = std::lower_bound(wds_.begin(), wds_.end(), w->d, CompareWatchDescriptor());
    wds_.insert(itd, w);
  }

  files_.insert(it, w);
  return true;
}

bool FileSystemWatcher::addPaths(const std::vector<std::string> &paths) {
  bool err = false;
  for (const std::string &path : paths)
    if (!addPath(path)) err = true;
  return !err;
}

bool FileSystemWatcher::removePath(const std::string &path) {
  WatchPath wp {path};
  std::vector<Watch *>::iterator itw = std::lower_bound(files_.begin(), files_.end(), wp, CompareWatch());
  if (itw == files_.end() || (*itw)->name != wp.name || (*itw)->directory != wp.directory) return false;
  remove_(*itw);
  std::vector<Watch *>::iterator itd = std::lower_bound(wds_.begin(), wds_.end(), (*itw)->d, CompareWatchDescriptor());
  warning_if(itd == wds_.end() || (*itd)->d != (*itw)->d) << LogStream::Color::Red << "itd == wds_.end() || (*itd)->d != (*itw)->d";
  inotify_rm_watch(notifyfd_, (*itd)->d);
  if (!(*itd)->name.empty()) wds_.erase(itd);
  else {
    WatchPath wp;
    wp.directory = (*itd)->directory;
    std::pair<std::vector<Watch *>::iterator, std::vector<Watch *>::iterator> itp = std::equal_range(files_.begin(), files_.end(), wp, CompareWatch());
    if (itp.second - itp.first == 1) {
      inotify_rm_watch(notifyfd_, (*itp.first)->d);
      trace() << LogStream::Color::DarkRed << "remove directory watch" << wp.directory << (*itp.first)->d;
      delete *itd;
      wds_.erase(itd);
    }
  }
  delete *itw;
  files_.erase(itw);
  return true;
}

bool FileSystemWatcher::removePaths(const std::vector<std::string> &paths) {
  bool err = false;
  for (const std::string &path : paths)
    if (!removePath(path)) err = true;
  return !err;
}

std::vector<std::string> FileSystemWatcher::paths() const {
  if (files_.empty()) return {};
  std::vector<std::string> _p;
  for (const Watch *f : files_) { _p.push_back(f->directory + '/' + f->name); }
  return _p;
}

std::string FileSystemWatcher::info() const {
  std::string str = "Files list:";
  if (files_.empty()) return str + " empty";
  for (const std::string &s : paths()) { str += '\n' + s; }
  return str;
}

void FileSystemWatcher::append_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  thread_->modifyTimer(timerid_, 1000);
  std::vector<const Watch *>::iterator it = std::find(we_.begin(), we_.end(), _w);
  if (it != we_.end()) return;
  we_.emplace_back(_w);
}

void FileSystemWatcher::remove_(const Watch *_w) {
  trace() << LogStream::Color::DarkRed << _w->directory << _w->name << _w->d;
  std::vector<const Watch *>::iterator it = std::find(we_.begin(), we_.end(), _w);
  if (it != we_.end()) we_.erase(it);
}

bool FileSystemWatcher::CompareWatch::operator()(const Watch *w1, const Watch *w2) const {
  int _r = w1->directory.compare(w2->directory);
  if (_r != 0) return (_r < 0);
  return w1->name.compare(w2->name) < 0;
}
bool FileSystemWatcher::CompareWatch::operator()(const WatchPath &p, const Watch *w) const {
  int _r = p.directory.compare(w->directory);
  if (_r != 0 || p.name.empty()) return (_r < 0);
  return p.name.compare(w->name) < 0;
}
bool FileSystemWatcher::CompareWatch::operator()(const Watch *w, const WatchPath &p) const {
  int _r = w->directory.compare(p.directory);
  if (_r != 0 || w->name.empty()) return (_r < 0);
  return w->name.compare(p.name) < 0;
}

bool FileSystemWatcher::CompareWatchDescriptor::operator()(const Watch *d1, const Watch *d2) const { return d1->d < d2->d; }
bool FileSystemWatcher::CompareWatchDescriptor::operator()(int fd, const Watch *d) const { return fd < d->d; }
bool FileSystemWatcher::CompareWatchDescriptor::operator()(const Watch *d, int fd) const { return d->d < fd; }
