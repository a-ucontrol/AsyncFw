/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

#if 1

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
  watcher.notify.connect([](const std::string &name, int event) {
    lsInfo() << LogStream::Color::DarkMagenta << "file:" << name << event;  // event: -1 removed / 0 changed / 1 created
    MainThread::exit();
  });

  File _f {"/tmp/FileSystemWatcher.example"};
  if (_f.exists()) {
    lsDebug() << "remove: tmp/FileSystemWatcher.example";
    _f.remove();
  } else {
    lsDebug() << "create: tmp/FileSystemWatcher.example";
    _f.open(std::ios::binary | std::ios::out);
  };

  lsInfo() << watcher;

  lsNotice() << "Start Application";
  int ret = MainThread::exec();
  lsNotice() << "End Application" << ret;

  return ret;
}
//! [snippet]

#else
  #include <AsyncFw/FileSystemWatcher>
  #include <AsyncFw/File>
  #include <AsyncFw/MainThread>
  #include <AsyncFw/Timer>
  #include <AsyncFw/LogStream>

  #include <filesystem>

using namespace AsyncFw;

int main(int argc, char *argv[]) {
  #ifdef USE_QAPPLICATION
  QCoreApplication app(argc, argv);
  #endif

  const std::string dir = "/tmp/FileSystemWatcher.example";
  const std::string f1 = dir + "/file1.txt";
  const std::string f2 = dir + "/file2.txt";
  const std::string f3 = dir + "/file3.txt";

  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  FileSystemWatcher watcher;
  int phase = 0;  // 0 = scenario A, 1 = scenario B
  bool scenarioAOk = false;
  int scenarioBNotify = 0;

  FunctionConnectionGuard g = watcher.notify.connect([&](const std::string &name, int event) {
    lsInfo() << "notify:" << name << event;

    if (phase == 0 && name == f2 && event == 1) {
      // Scenario A success: f2 was created after removePath(f1), even
      // though f1 and f2 shared one directory wd.
      scenarioAOk = true;
    }
    if (phase == 1 && name == f3 && event == 0) { scenarioBNotify++; }
  });

  // ---- Scenario A ------------------------------------------------------
  lsInfo() << "=== Scenario A: shared directory watch ===";

  // Both pending: registers a single shared dir wd.
  watcher.addPath(f1);
  watcher.addPath(f2);
  lsInfo() << watcher;

  // BUG (pre-fix): kills the shared wd, f2 loses observation.
  watcher.removePath(f1);
  lsInfo() << "after removePath(file1)";
  lsInfo() << watcher;

  // Create f2 -- watcher must still report Created(f2).
  {
    File _f {f2};
    _f.open(std::ios::binary | std::ios::out);
  }

  // ---- Scenario B ------------------------------------------------------
  // Runs 300 ms later, after Scenario A's notification has been delivered.
  Timer::single(300, [&]() {
    lsInfo() << "=== Scenario B: file watch lifecycle ===";
    phase = 1;

    // f3 exists -> addPath registers an exclusive file wd.
    {
      File _f {f3};
      _f.open(std::ios::binary | std::ios::out);
    }
    watcher.addPath(f3);
    lsInfo() << watcher;

    // removePath(f3) must call inotify_rm_watch() for the file wd.
    watcher.removePath(f3);
    lsInfo() << "after removePath(file3)";
    lsInfo() << watcher;

    // Re-add the same existing file: must register a fresh wd.
    watcher.addPath(f3);
    lsInfo() << "after re-addPath(file3)";
    lsInfo() << watcher;

    // Write f3 -- expect exactly one Changed on the new wd.
    {
      File _f {f3};
      _f.open(std::ios::binary | std::ios::out | std::ios::app);
      _f.write("b", 1);
    }
  });

  // Scenario C: delete + recreate existing file
  {
    const std::string f4 = dir + "/file4.txt";
    {
      File _f {f4};
      _f.open(std::ios::binary | std::ios::out);
    }
    watcher.addPath(f4);

    std::filesystem::remove(f4);
    Timer::single(300, [f4]() {  // ← по значению
      {
        File _f {f4};
        _f.open(std::ios::binary | std::ios::out);
      }
    });
  }

  // ---- Final report ----------------------------------------------------
  Timer::single(1000, [&]() {
    lsNotice() << "Scenario A:" << (scenarioAOk ? "OK (Created(f2) received)" : "BUG (Created(f2) not received)");
    lsNotice() << "Scenario B: Changed(f3) count =" << scenarioBNotify << (scenarioBNotify == 1 ? "-> OK" : "-> BUG (expected exactly 1)");
    MainThread::exit();
  });

  lsNotice() << "Start Application";
  int ret = MainThread::exec();
  lsNotice() << "End Application" << ret;
  return ret;
}
#endif