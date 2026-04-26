/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

#include <string>
#include "../core/FunctionConnector.h"
#include "../main/Instance.h"

namespace AsyncFw {
/*! \class FileSystemWatcher FileSystemWatcher.h <AsyncFw/FileSystemWatcher> \brief The FileSystemWatcher class. \warning Unix-like systems only
 \brief Example: \snippet FileSystemWatcher/main.cpp snippet */
class FileSystemWatcher {
  friend LogStream &operator<<(LogStream &, const FileSystemWatcher &);

public:
  static FileSystemWatcher *instance() { return instance_.value; }
  FileSystemWatcher(const std::vector<std::string> & = {});
  virtual ~FileSystemWatcher();
  bool addPath(const std::string &path);
  bool addPaths(const std::vector<std::string> &paths);
  bool removePath(const std::string &path);
  bool removePaths(const std::vector<std::string> &paths);
  std::vector<std::string> paths() const;

  /*! \brief The FunctionConnector for notification of file-related events. */
  FunctionConnectorProtected<FileSystemWatcher>::Connector<const std::string &, int> notify;

private:
  static inline Instance<FileSystemWatcher> instance_ {"FileSystemWatcher"};
  struct Private;
  Private *private_;
};
}  // namespace AsyncFw
