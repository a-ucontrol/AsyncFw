/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

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

  FileSystemWatcher watcher {{"/tmp/FileSystemWatcher.example/file.txt"}};
  watcher.notify.connect([](const std::string &name, int event) {
    lsInfo() << LogStream::Color::DarkMagenta << "file:" << name << event;  // event: FileSystemWatcher::Event
    MainThread::exit();
  });

  File _f {"/tmp/FileSystemWatcher.example/file.txt"};
  if (_f.exists()) {
    lsDebug() << "remove: tmp/FileSystemWatcher.example/file.txt";
    _f.remove();
  } else {
    lsDebug() << "create: tmp/FileSystemWatcher.example/file.txt";
    _f.open(std::ios::binary | std::ios::out);
  };

  lsInfo() << watcher;

  lsNotice() << "Start Application";
  int ret = MainThread::exec();
  lsNotice() << "End Application" << ret;

  return ret;
}
//! [snippet]
