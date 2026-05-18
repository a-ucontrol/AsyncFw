/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
/*
 * This exfample for Unix-like systems only
*/
#include <AsyncFw/MainThread>
#include <AsyncFw/PollNotifier>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  AsyncFw::PollNotifier notifier(STDIN_FILENO);
  notifier.notify.connect([&notifier](AsyncFw::AbstractThread::PollEvents e) {
    char buf[128];
    int r = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    buf[r] = 0;
    if (r == 2 && buf[0] == 'q') AsyncFw::MainThread::exit();
    (lsDebug() << "stdin: " << buf).flush();
  });
  (lsDebug() << "Start Applicaiton").flush();
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
//! [snippet]
