#define PIPE_E
#ifdef PIPE_E
#include <AsyncFw/Thread>
#include <AsyncFw/LogStream>
#include <AsyncFw/MainThread>

#include <atomic>
#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

using namespace AsyncFw;

struct PollCb : public AbstractThread::AbstractPollTask {
  int fd = -1;
  std::atomic<int> *counter = nullptr;

  PollCb() = default;
  PollCb(int f, std::atomic<int> *c) : fd(f), counter(c) {}

  void operator()(AbstractThread::PollEvents e) override {
    if (counter) ++(*counter);
    // level-triggered io_uring poll: MUST drain the source, otherwise
    // POLLIN stays ready and the callback fires on every loop iteration.
    char buf[64];
    while (::read(fd, buf, sizeof(buf)) > 0) {}
    (void)e;
    lsNotice() << "=== READED ===" << fd;
  }
};

static void makePipe(int p[2]) {
  if (pipe(p) < 0) { lsError() << "pipe failed"; std::abort(); }
  fcntl(p[0], F_SETFL, O_NONBLOCK);
  fcntl(p[1], F_SETFL, O_NONBLOCK);
}

static void sleep_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Correct restart pattern (contract A):
//   - register once
//   - quit/wait/start as many times as needed
//   - same SQE lives in the io_uring ring across restarts, callbacks still fire
//   - remove once at the end
//
// IMPORTANT: PollCb is owned by the thread once appendPollDescriptor returns.
// removePollDescriptor / quit do NOT free it synchronously; the actual delete
// happens asynchronously in destroy_removed_polls() inside the worker thread.
// Therefore the counter is kept OUTSIDE PollCb (on the test's stack), and
// reading it after waitFinished() is safe.
// ---------------------------------------------------------------------------
static void testQuitRestartCorrect() {
  lsNotice() << "=== testQuitRestartCorrect ===";
  Thread t("QuitRestart");
  int p[2]; makePipe(p);
  int p2[2]; makePipe(p2);
  std::atomic<int> counter{0};
  auto *cb = new PollCb{p[0], &counter};
  auto *cb2 = new PollCb{p2[0], &counter};

  t.start();
  t.appendPollDescriptor(p[0], AbstractThread::PollIn, cb);
  t.appendPollDescriptor(p2[0], AbstractThread::PollIn, cb2);
  sleep_ms(2);

  char c = 'x';
  ::write(p2[1], &c, 1);

  for (int round = 0; round < 3; ++round) {
    lsInfoGreen() << "restart round" << round;
    char c = 'x';
    ::write(p[1], &c, 1);
    lsInfoMagenta() << "WRITE" << p[1];
//    sleep_ms(3);

    if(round == 1) t.modifyPollDescriptor(p2[0], AsyncFw::AbstractThread::PollNo);
    else if(round == 2) t.modifyPollDescriptor(p2[0], AsyncFw::AbstractThread::PollNo);

    t.quit();
    t.waitFinished();
    t.start();
    sleep_ms(2);
  }

  t.quit();
  ::write(p2[1], &c, 1);
//  t.modifyPollDescriptor(p[0], AsyncFw::AbstractThread::PollNo);
  t.removePollDescriptor(p2[0]);
  t.waitFinished();
  ::close(p[0]);
  ::close(p[1]);

  ::close(p2[0]);
  ::close(p2[1]);
  lsInfoGreen() << "callbacks received:" << counter.load();
  assert(counter.load() >= 3);  // at least one per round
}

// ---------------------------------------------------------------------------
// Incorrect restart pattern (contract A violation):
//   - register, quit without removePollDescriptor
//   - restart and try to register the SAME fd again
//   - must return false (poll task for fd is still in poll_tasks)
//   - cleanup must still work via removePollDescriptor
//
// Ownership notes:
//   - cb1 will be freed asynchronously by the thread, never read after this.
//   - cb2 is freed inside appendPollDescriptor on the failure path (the
//     framework does `delete task;` when it rejects a duplicate), so we must
//     not touch it afterwards.
//   - cb3 is a fresh registration after proper cleanup.
// ---------------------------------------------------------------------------
static void testQuitRestartWrongPattern() {
  lsNotice() << "=== testQuitRestartWrongPattern ===";
  Thread t("QuitRestartWrong");
  int p[2]; makePipe(p);
  std::atomic<int> counter1{0};
  std::atomic<int> counter3{0};
  auto *cb1 = new PollCb{p[0], &counter1};

  t.start();
  t.invoke([&]() { assert(t.appendPollDescriptor(p[0], AbstractThread::PollIn, cb1)); }, true);
  sleep_ms(2);

  // quit without removing
  t.quit();
  t.waitFinished();

  // restart and try again with the same fd
  t.start();
  auto *cb2 = new PollCb{p[0], nullptr};
  bool ok = false;
  t.invoke([&]() { ok = t.appendPollDescriptor(p[0], AbstractThread::PollIn, cb2); }, true);
  // poll_tasks still holds the fd from the previous session.
  assert(!ok && "expected append to fail: descriptor already exists");
  cb2 = nullptr;  // freed by the framework on the failure path

  // proper cleanup: remove the original registration, quit, restart, verify empty
  t.invoke([&]() { t.removePollDescriptor(p[0]); }, true);
  t.quit();
  t.waitFinished();

  // now restart is clean: re-register must succeed
  t.start();
  auto *cb3 = new PollCb{p[0], &counter3};
  bool ok3 = false;
  t.invoke([&]() { ok3 = t.appendPollDescriptor(p[0], AbstractThread::PollIn, cb3); }, true);
  assert(ok3 && "expected append to succeed after proper removePollDescriptor");

  t.invoke([&]() { t.removePollDescriptor(p[0]); }, true);
  t.quit();
  t.waitFinished();

  ::close(p[0]);
  ::close(p[1]);
}

int main(int argc, char *argv[]) {
  LogStream::setTimeFormat("%Y-%m-%d %H:%M:%S", true);

  testQuitRestartCorrect();
  testQuitRestartWrongPattern();

  lsNotice() << "RESTART TESTS PASSED";
  return 0;
}
  #else

  /*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

  #include <AsyncFw/Thread>
  #include <AsyncFw/LogStream>
  #include <AsyncFw/MainThread>

  #include <atomic>
  #include <cassert>
  #include <chrono>
  #include <fcntl.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>

using namespace AsyncFw;

struct PollCb : public AbstractThread::AbstractPollTask {
  int fd = -1;
  std::atomic<int> *counter = nullptr;

  PollCb() = default;
  PollCb(int f, std::atomic<int> *c) : fd(f), counter(c) {}

  void operator()(AbstractThread::PollEvents e) override {
    if (counter) ++(*counter);
    // level-triggered io_uring poll: MUST drain the source, otherwise
    // POLLIN stays ready and the callback fires on every loop iteration.
    char buf[64];
    while (::read(fd, buf, sizeof(buf)) > 0) {}
    (void)e;
    lsNotice() << "=== READED ===" << fd;
  }
};

static void makeSocketpair(int sv[2]) {
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    lsError() << "socketpair failed";
    std::abort();
  }
  fcntl(sv[0], F_SETFL, O_NONBLOCK);
  fcntl(sv[1], F_SETFL, O_NONBLOCK);
}

static void sleep_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// Correct restart pattern (contract A):
//   - register once
//   - quit/wait/start as many times as needed
//   - same SQE lives in the io_uring ring across restarts, callbacks still fire
//   - remove once at the end
//
// IMPORTANT: PollCb is owned by the thread once appendPollDescriptor returns.
// removePollDescriptor / quit do NOT free it synchronously; the actual delete
// happens asynchronously in destroy_removed_polls() inside the worker thread.
// Therefore the counter is kept OUTSIDE PollCb (on the test's stack), and
// reading it after waitFinished() is safe.
// ---------------------------------------------------------------------------
static void testQuitRestartCorrect() {
  lsNotice() << "=== testQuitRestartCorrect ===";
  Thread t("QuitRestart");
  int s1[2]; makeSocketpair(s1);
  int s2[2]; makeSocketpair(s2);
  std::atomic<int> counter{0};
  auto *cb  = new PollCb{s1[0], &counter};
  auto *cb2 = new PollCb{s2[0], &counter};

  t.start();
  t.appendPollDescriptor(s1[0], AbstractThread::PollIn, cb);
  t.appendPollDescriptor(s2[0], AbstractThread::PollIn, cb2);
  sleep_ms(2);

  char c = 'x';
  ::write(s2[1], &c, 1);

  for (int round = 0; round < 3; ++round) {
    lsInfoGreen() << "restart round" << round;
    char c = 'x';
    ::write(s1[1], &c, 1);
    lsInfoMagenta() << "WRITE" << s1[1];
    sleep_ms(3);
    t.quit();
    t.waitFinished();
    t.start();
    sleep_ms(2);
  }

  t.quit();
  ::write(s2[1], &c, 1);
  t.removePollDescriptor(s1[0]);
  t.removePollDescriptor(s2[0]);
  t.waitFinished();
  ::close(s1[0]);
  ::close(s1[1]);
  ::close(s2[0]);
  ::close(s2[1]);
  lsInfoGreen() << "callbacks received:" << counter.load();
  assert(counter.load() >= 3);  // at least one per round
}

// ---------------------------------------------------------------------------
// Incorrect restart pattern (contract A violation):
//   - register, quit without removePollDescriptor
//   - restart and try to register the SAME fd again
//   - must return false (poll task for fd is still in poll_tasks)
//   - cleanup must still work via removePollDescriptor
//
// Ownership notes:
//   - cb1 will be freed asynchronously by the thread, never read after this.
//   - cb2 is freed inside appendPollDescriptor on the failure path (the
//     framework does `delete task;` when it rejects a duplicate), so we must
//     not touch it afterwards.
//   - cb3 is a fresh registration after proper cleanup.
// ---------------------------------------------------------------------------
static void testQuitRestartWrongPattern() {
  lsNotice() << "=== testQuitRestartWrongPattern ===";
  Thread t("QuitRestartWrong");
  int sv[2]; makeSocketpair(sv);
  std::atomic<int> counter1{0};
  std::atomic<int> counter3{0};
  auto *cb1 = new PollCb{sv[0], &counter1};

  t.start();
  t.invoke([&]() { assert(t.appendPollDescriptor(sv[0], AbstractThread::PollIn, cb1)); }, true);
  sleep_ms(2);

  // quit without removing
  t.quit();
  t.waitFinished();

  // restart and try again with the same fd
  t.start();
  auto *cb2 = new PollCb{sv[0], nullptr};
  bool ok = false;
  t.invoke([&]() { ok = t.appendPollDescriptor(sv[0], AbstractThread::PollIn, cb2); }, true);
  // poll_tasks still holds the fd from the previous session.
  assert(!ok && "expected append to fail: descriptor already exists");
  cb2 = nullptr;  // freed by the framework on the failure path

  // proper cleanup: remove the original registration, quit, restart, verify empty
  t.invoke([&]() { t.removePollDescriptor(sv[0]); }, true);
  t.quit();
  t.waitFinished();

  // now restart is clean: re-register must succeed
  t.start();
  auto *cb3 = new PollCb{sv[0], &counter3};
  bool ok3 = false;
  t.invoke([&]() { ok3 = t.appendPollDescriptor(sv[0], AbstractThread::PollIn, cb3); }, true);
  assert(ok3 && "expected append to succeed after proper removePollDescriptor");

  t.invoke([&]() { t.removePollDescriptor(sv[0]); }, true);
  t.quit();
  t.waitFinished();

  ::close(sv[0]);
  ::close(sv[1]);
}

int main(int argc, char *argv[]) {
  LogStream::setTimeFormat("%Y-%m-%d %H:%M:%S", true);

  testQuitRestartCorrect();
  testQuitRestartWrongPattern();

  lsNotice() << "RESTART TESTS PASSED";
  return 0;
}


  #endif