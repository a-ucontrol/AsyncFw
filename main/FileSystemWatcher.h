/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#pragma once

/** @file FileSystemWatcher.h @brief The FileSystemWatcher class. */

#include <string>
#include "../core/FunctionConnector.h"
#include "../main/Instance.h"

namespace AsyncFw {
/** @class FileSystemWatcher FileSystemWatcher.h <AsyncFw/FileSystemWatcher> @brief Provides an asynchronous, cross-platform interface for monitoring specific files.
@details This class tracks file lifecycle events (creation, modifications, and deletion). @n It wraps platform-specific mechanisms (inotify on Linux, QFileSystemWatcher on Windows) to guarantee identical, deterministic behavior across operating systems.
@note **Important Design Constraints:**
- **File-Centric Design:** This class is engineered primarily to monitor specific file paths.
- **Windows Wildcard Limitation:** Broad directory masks or wildcard paths (e.g., /tmp/\*) are **not supported on Windows + Qt6** and will cause addPath to fail. This usage is valid only under native Linux implementations.
- **Debouncing Mechanism:** In-flight modifications (continuous bursts of write operations) are automatically aggregated. A single Changed notification is dispatched exactly 1 second after the file system activity settles down.
@brief Example: @snippet FileSystemWatcher/main.cpp snippet */
class FileSystemWatcher {
  friend LogStream &operator<<(LogStream &, const FileSystemWatcher &);
  /** @enum Event @brief Internal mapping of the file system events. */
  enum Event : int8_t {
    Removed = -1,  ///< The monitored file was physically deleted or renamed out of its directory.
    Changed = 0,   ///< The contents or critical attributes of the file were modified.
    Created = 1    ///< A previously missing monitored file was successfully created on disk.
  };

public:
  /** @brief Retrieves the global instance pointer of the active FileSystemWatcher. @return Pointer to the active FileSystemWatcher instance, or nullptr if none exists. */
  static FileSystemWatcher *instance() { return instance_.value; }
  /** @brief Constructs a FileSystemWatcher and optionally starts monitoring given paths. @param paths A collection of absolute or relative specific file paths to track. */
  FileSystemWatcher(const std::vector<std::string> & = {});
  /** @brief Destроys the watcher and cleans up all underlying platform-specific event filters. */
  virtual ~FileSystemWatcher();
  /** @brief Registers a single explicit file path into the observation pool.
  @details If the file does not exist at the time of calling, the watcher begins tracking its parent directory to catch the creation event asynchronously.
  @param path The path of the specific file to monitor. @return True if the path was successfully validated and scheduled for monitoring */
  bool addPath(const std::string &path);
  /** @brief Registers multiple file paths into the observation pool. @param paths A vector of specific file paths to add. @return True if all paths were added successfully. */
  bool addPaths(const std::vector<std::string> &paths);
  /** @brief Unregisters a monitored file path, halting all associated event signals. @param path The exact monitored file path to remove. @return True if the file was found and safely removed. */
  bool removePath(const std::string &path);
  /** @brief Unregisters multiple file paths from the observation pool. @param paths A vector of monitored file paths to remove. @return True if all requested paths was found and removed successfully. */
  bool removePaths(const std::vector<std::string> &paths);
  /** @brief Obtains a list of all currently registered file paths. @return A vector of strings representing the fully normalized monitored paths. */
  std::vector<std::string> paths() const;
  /** @brief The FunctionConnector for notification of file-related events.
  @details Sends the normalized full path to the file that triggered the event, and specify the event type: **(Deleted)**, **(Modified)**, or **(Created)**. */
  FunctionConnector<const std::string &, int>::Protected<FileSystemWatcher> notify;

private:
  static Instance<FileSystemWatcher> instance_;
  struct Private;
  Private &private_;
};
}  // namespace AsyncFw
