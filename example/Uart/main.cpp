/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
/*
 * This exfample for Unix-like systems only
*/
#include <termios.h>
#include <fcntl.h>
#include <core/LogStream.h>
#include <MainThread.h>
#include <PollNotifier.h>

int main(int argc, char *argv[]) {
  const char _ttyS[] = "/dev/ttyUSB0";
  int _rate = 115200;

  termios tio {};
  tio.c_cflag = CS8;
  int fd;
  if ((fd = open(_ttyS, O_RDWR | O_NONBLOCK)) < 0) {
    logWarning() << "Open failed.";
    return -1;
  }
  cfsetospeed(&tio, _rate);
  cfsetispeed(&tio, _rate);
  tcsetattr(fd, TCSANOW, &tio);

  std::string str;
  AsyncFw::PollNotifier notifier(fd);
  notifier.notify([&notifier, &fd, &str](AsyncFw::AbstractThread::PollEvents) {
    char buf[128];
    int r = read(fd, buf, sizeof(buf) - 1);
    if (r > 0) {
      buf[r] = 0;
      str += buf;
      for (;;) {
        int i = str.find('\n');
        if (i == std::string::npos) break;
        lsDebug() << "readed: " << str.substr(0, ++i);
        str.erase(0, i);
      }
    }
  });

  lsNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton" << ret;
  return ret;
}
//! [snippet]
