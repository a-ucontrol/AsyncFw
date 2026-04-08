/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/FileSystemWatcher>
#include <AsyncFw/File>
#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

using namespace AsyncFw;

int main(int argc, char *argv[]) {
#ifdef USE_QAPPLICATION
  QCoreApplication app(argc, argv);
#endif

  FileSystemWatcher watcher {{"/tmp/FileSystemWatcher.example"}};
  watcher.notify([](const std::string &name, int event) {
    lsInfoMagenta() << "file:" << name << event;  // event: -1 removed / 0 changed / 1 created
    MainThread::exit();
  });

  File _f {"/tmp/FileSystemWatcher.example"};
  if (_f.exists()) _f.remove();
  else { _f.open(std::ios::binary | std::ios::out); };

  lsInfoGreen() << watcher;

  lsNotice() << "Start Applicaiton";
  int ret = MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;

  return ret;
}
//! [snippet]
