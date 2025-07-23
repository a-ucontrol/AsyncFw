#pragma once

#include <string>
#include "core/FunctionConnector.h"

namespace AsyncFw {
class FileSystemWatcher {
public:
  static FileSystemWatcher *instance() { return fileSystemWatcher; }
  FileSystemWatcher(const std::vector<std::string> & = {});
  ~FileSystemWatcher();
  bool addPath(const std::string &path);
  bool addPaths(const std::vector<std::string> &paths);
  bool removePath(const std::string &path);
  bool removePaths(const std::vector<std::string> &paths);
  std::vector<std::string> paths() const;
  std::string info() const;

  FunctionConnectorProtected<FileSystemWatcher>::Connector<const std::string &, int> watch;

private:
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
  static inline FileSystemWatcher *fileSystemWatcher;
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
    bool operator()(int, const Watch *) const;
    bool operator()(const Watch *, int) const;
  };
};
}  // namespace AsyncFw
