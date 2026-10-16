// Core test suite.
// Groups:
//   A. FunctionConnector — pub/sub, policies, guards, concurrency.
//   B. AbstractThread   — ~Thread waits for a running task.
//   C. MainThread       — repeated exec() does not leak fds.
//   D. AbstractSocket   — HUP/ERR error classification.
//   E. AbstractThread::Waiter
//
// All groups share one main() and one EXPECT harness.

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Thread>
#include <AsyncFw/Timer>
#include <AsyncFw/AbstractSocket>
#include <AsyncFw/LogStream>

#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifndef _WIN32
  #include <dirent.h>
#endif

using namespace AsyncFw;
using Conn = AbstractFunctionConnector::Connection;

namespace {

// ============================================================================
// Test harness
// ============================================================================

std::atomic<int> g_failed{0};
std::atomic<int> g_total{0};

#define EXPECT(cond)                                                                        \
do {                                                                                      \
      ++g_total;                                                                              \
      if (!(cond)) {                                                                          \
        ++g_failed;                                                                           \
        lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond; \
  }                                                                                       \
} while (0)

    template <typename F>
    bool wait_for(F pred, int ms = 2000) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

// Copy/move tracker — used to prove that multicast emit does not move-steal
// from the first subscriber (see comment in core/invocable.hpp).
struct Tracker {
  std::unique_ptr<int> p;
  static std::atomic<int> copies;
  static std::atomic<int> moves;

  explicit Tracker(int v) : p(std::make_unique<int>(v)) {}
  Tracker(const Tracker &o) : p(std::make_unique<int>(*o.p)) { ++copies; }
  Tracker(Tracker &&o) noexcept : p(std::move(o.p)) { ++moves; }
  Tracker &operator=(const Tracker &) = delete;
  Tracker &operator=(Tracker &&) = delete;
};
std::atomic<int> Tracker::copies{0};
std::atomic<int> Tracker::moves{0};

// ============================================================================
// Group A. FunctionConnector
// ============================================================================

void test_direct_same_thread() {
  FunctionConnector<int> fc;
  int got = 0;
  fc.connect<Conn::Direct>([&](int v) { got = v; });
  fc(42);
  EXPECT(got == 42);
}

void test_direct_zero_args() {
  FunctionConnector<> fc;
  int n = 0;
  fc.connect<Conn::Direct>([&]() { ++n; });
  fc();
  fc();
  EXPECT(n == 2);
}

void test_multiple_direct_subscribers() {
  FunctionConnector<int> fc;
  int a = 0, b = 0, c = 0;
  fc.connect<Conn::Direct>([&](int v) { a = v; });
  fc.connect<Conn::Direct>([&](int v) { b = v * 2; });
  fc.connect<Conn::Direct>([&](int v) { c = v * 3; });
  fc(5);
  EXPECT(a == 5);
  EXPECT(b == 10);
  EXPECT(c == 15);
}

void test_argument_copy_semantics_direct() {
  Tracker::copies = 0;
  Tracker::moves = 0;
  FunctionConnector<Tracker> fc;
  int seen1 = -1, seen2 = -1;
  fc.connect<Conn::Direct>([&](Tracker t) { seen1 = *t.p; });
  fc.connect<Conn::Direct>([&](Tracker t) { seen2 = *t.p; });

  Tracker original{7};
  int copies_before = Tracker::copies.load();
  fc(original);

  EXPECT(seen1 == 7);
  EXPECT(seen2 == 7);
  EXPECT(*original.p == 7);
  EXPECT(Tracker::copies.load() >= copies_before + 2);
}

void test_reentrant_direct() {
  FunctionConnector<int> fc;
  int count = 0;
  fc.connect<Conn::Direct>([&](int v) {
    ++count;
    if (v > 0) fc(v - 1);
  });
  fc(3);
  EXPECT(count == 4);  // 3, 2, 1, 0
}

void test_disconnect_during_emit() {
  FunctionConnector<int> fc;
  int a = 0, b = 0;
  FunctionConnectionGuard gb;

  FunctionConnectionGuard ga = fc.connect<Conn::Direct>([&](int) {
    ++a;
    gb = {};  // disconnect b
  });
  gb = fc.connect<Conn::Direct>([&](int) { ++b; });

  fc(0);
  EXPECT(a == 1);
  EXPECT(b == 1);
  fc(0);
  EXPECT(a == 2);
  EXPECT(b == 1);
}

void test_guard_raii() {
  FunctionConnector<int> fc;
  int n = 0;
  {
    FunctionConnectionGuard g = fc.connect<Conn::Direct>([&](int) { ++n; });
    fc(0);
    EXPECT(n == 1);
  }
  fc(0);
  EXPECT(n == 1);
}

void test_guard_move() {
  FunctionConnector<int> fc;
  int n = 0;
  FunctionConnectionGuard g1 = fc.connect<Conn::Direct>([&](int) { ++n; });
  EXPECT(static_cast<bool>(g1));
  FunctionConnectionGuard g2 = std::move(g1);
  EXPECT(!g1);
  EXPECT(static_cast<bool>(g2));
  fc(0);
  EXPECT(n == 1);
}

void test_guard_list() {
  FunctionConnector<int> fc;
  int n = 0;
  {
    FunctionConnectionGuardList gl;
    gl += fc.connect<Conn::Direct>([&](int) { n += 1; });
    gl += fc.connect<Conn::Direct>([&](int) { n += 10; });
    fc(0);
    EXPECT(n == 11);
  }
  fc(0);
  EXPECT(n == 11);
}

void test_auto_same_thread() {
  FunctionConnector<int> fc;
  int got = 0;
  fc.connect([&](int v) { got = v; });
  fc(11);
  EXPECT(got == 11);
}

void test_policy_direct_only() {
  FunctionConnector<int>::Policy<AbstractFunctionConnector::DirectOnly> fc;
  auto caller_id = std::this_thread::get_id();
  bool same = false;
  fc.connect([&](int) { same = (std::this_thread::get_id() == caller_id); });
  fc(0);
  EXPECT(same);
}

void test_queued_cross_thread() {
  Thread target("test-queued");
  target.start();

  FunctionConnector<int> fc;
  std::atomic<int> got{0};
  std::atomic<bool> done{false};

  target.invoke([&]() {
    fc.connect<Conn::Queued>([&](int v) {
      got = v;
      done = true;
    });
  }, true);

  fc(99);
  EXPECT(wait_for([&] { return done.load(); }));
  EXPECT(got == 99);
}

void test_sync_cross_thread() {
  Thread target("test-sync");
  target.start();

  FunctionConnector<int> fc;
  std::atomic<int> got{0};

  target.invoke([&]() { fc.connect<Conn::Sync>([&](int v) { got = v; }); }, true);

  fc(123);
  EXPECT(got == 123);
}

void test_queued_deferred_not_inline() {
  Thread target("test-deferred");
  target.start();

  bool seen_inline = false;
  std::atomic<int> got{0};
  std::atomic<bool> ready{false};

  target.invoke([&]() {
    FunctionConnector<int> fc;
    fc.connect<Conn::Queued>([&](int v) {
      got = v;
      seen_inline = true;
    });
    fc(7);
    EXPECT(!seen_inline);
    Timer::single(20, [&]() { ready = true; });
  }, true);

  EXPECT(wait_for([&] { return ready.load(); }));
  EXPECT(got == 7);
  EXPECT(seen_inline);
}

class Emitter {
public:
  FunctionConnector<int>::Protected<Emitter> signal;
  void fire(int v) { signal(v); }
};

void test_protected_emitter() {
  Emitter e;
  int got = 0;
  e.signal.connect<Conn::Direct>([&](int v) { got = v; });
  e.fire(55);
  EXPECT(got == 55);
}

void test_independent_connections() {
  FunctionConnector<int> fc;
  int n = 0;
  auto callable = [&](int) { ++n; };
  auto &c1 = fc.connect<Conn::Direct>(callable);
  auto &c2 = fc.connect<Conn::Direct>(callable);

  fc(0);
  EXPECT(n == 2);

  {
    FunctionConnectionGuard g(c1);
    fc(0);
    EXPECT(n == 4);
  }

  fc(0);
  EXPECT(n == 5);
  (void)c2;
}

void test_guard_destroy_during_concurrent_emit() {
  FunctionConnector<int> fc;
  std::atomic<int> received{0};
  std::atomic<bool> stop{false};

  FunctionConnectionGuard *guard = new FunctionConnectionGuard(
      fc.connect<Conn::Direct>([&](int v) {
    received.fetch_add(v, std::memory_order_relaxed);
    std::this_thread::yield();
  }));

  std::thread emitter([&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      fc(1);
      std::this_thread::yield();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  delete guard;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop = true;
  emitter.join();

  lsNotice() << "test_guard_destroy_during_concurrent_emit: received =" << received.load();
  EXPECT(received.load() > 0);
}

// Guard destroyed in one AsyncFw::Thread, connector destroyed in another.
//
// Declaration order matters:
//   Lambdas sent via invoke() run asynchronously and read ready/go/done by
//   reference. C++ destroys locals in reverse declaration order. If ready/go
//   are declared after tA/tB, they are destroyed before ~Thread, while the
//   lambdas still run — ASan reports stack-use-after-scope.
//
// Why done:
//   ~Thread blocks until exec() actually finishes, but relying on that alone
//   is fragile (implicit contract). done makes "both lambdas finished reading
//   go/ready" explicit before leaving the scope.
void test_guard_and_connector_concurrent_destroy_asyncfw() {
  for (int i = 0; i < 200; ++i) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<int> done{0};

    auto *fc = new FunctionConnector<int>;
    auto *g = new FunctionConnectionGuard(
        fc->connect<Conn::Direct>([](int) {}));

    Thread tA("destroy-guard");
    Thread tB("destroy-connector");
    tA.start();
    tB.start();

    tA.invoke([&]() {
      ready.fetch_add(1);
      while (!go.load()) std::this_thread::yield();
      delete g;
      g = nullptr;
      done.fetch_add(1);
    });
    tB.invoke([&]() {
      ready.fetch_add(1);
      while (!go.load()) std::this_thread::yield();
      delete fc;
      fc = nullptr;
      done.fetch_add(1);
    });

    while (ready.load() < 2) std::this_thread::yield();
    go = true;
    while (done.load() < 2) std::this_thread::yield();
    // ~tB, ~tA run here in reverse declaration order.
  }
  EXPECT(true);
}

// ============================================================================
// Group B. AbstractThread
// ============================================================================

// ~Thread must block the caller until the currently running task completes.
// If it returns early, the task is still running with references to
// stack locals declared before the Thread object, which ASan catches as
// stack-use-after-scope, and taskFinished can be observed as false.
void test_thread_waits_for_running_task() {
  std::atomic<bool> taskFinished{false};
  {
    std::atomic<bool> taskStarted{false};
    Thread t("probe");
    t.start();
    t.invoke([&]() {
      taskStarted = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      taskFinished = true;
    });
    while (!taskStarted.load()) std::this_thread::yield();
    // ~t runs here and must block until the task above completes.
  }
  EXPECT(taskFinished.load());
}

// ============================================================================
// Group C. MainThread re-exec
// ============================================================================

int open_fd_count() {
#ifndef _WIN32
  int n = 0;
  DIR *d = opendir("/proc/self/fd");
  if (!d) return -1;
  while (struct dirent *e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    ++n;
  }
  closedir(d);
  return n;
#else
  return 0;
#endif
}

void test_mainthread_reexec() {
  // Subscribe to MainThread lifetime events. The guards are stored so the
  // subscriptions are dropped when the test returns — otherwise a later
  // MainThread::exec() would fire dangling lambdas that reference the
  // counters below by reference.
  std::atomic<int> started_count{0};
  std::atomic<int> finished_count{0};
  FunctionConnectionGuardList guards;
  guards += FunctionConnectionGuard(
      Thread::current()->started.connect<Conn::Direct>([&]() { ++started_count; }));
  guards += FunctionConnectionGuard(
      Thread::current()->finished.connect<Conn::Direct>([&]() { ++finished_count; }));

  int fds_before = open_fd_count();

  // -------- first exec ------------------------------------------------
  {
    bool timer_fired = false;
    Timer::single(10, [&timer_fired]() {
      timer_fired = true;
      MainThread::exit(0);
    });
    int r1 = MainThread::exec();
    EXPECT(r1 == 0);
    EXPECT(timer_fired);
  }
  int fds_after_1 = open_fd_count();

  // -------- second exec -----------------------------------------------
  {
    bool timer_fired = false;
    Timer::single(10, [&timer_fired]() {
      timer_fired = true;
      MainThread::exit(42);
    });
    int r2 = MainThread::exec();
    EXPECT(r2 == 42);
    EXPECT(timer_fired);
  }
  int fds_after_2 = open_fd_count();

  lsNotice() << "started:" << started_count.load() << "finished:" << finished_count.load();
  lsNotice() << "fds: before=" << fds_before << "after 1st=" << fds_after_1 << "after 2nd=" << fds_after_2;

  // eventfd (or exit_pipe) is created exactly once, on the first `started`.
  EXPECT(fds_after_1 == fds_before + 1);
  // Second exec re-emits `started`; the setup guard makes it idempotent,
  // so no additional descriptors are opened.
  EXPECT(fds_after_2 == fds_after_1);
}

// ============================================================================
// Group D. AbstractSocket HUP/ERR classification
// ============================================================================

struct Trace {
  std::vector<int> states;
  std::vector<int> errors;
  std::vector<std::string> strings;
  bool sawConnected = false;
  bool sawActive = false;
  bool sawRefused = false;
  bool sawClosed = false;
  bool reachedUnconnected = false;

  void record(int state, int err, const std::string &es) {
    states.push_back(state);
    errors.push_back(err);
    strings.push_back(es);
    if (state == AbstractSocket::Connected) sawConnected = true;
    if (state == AbstractSocket::Active) sawActive = true;
    if (err == AbstractSocket::Refused) sawRefused = true;
    if (err == AbstractSocket::Closed) sawClosed = true;
    if (state == AbstractSocket::Unconnected) reachedUnconnected = true;
  }

  std::string dump() const {
    std::string r;
    for (size_t i = 0; i < states.size(); ++i) {
      if (i) r += " -> ";
      r += std::to_string(states[i]) + "/" + std::to_string(errors[i]);
      if (!strings[i].empty()) r += "(\"" + strings[i] + "\")";
    }
    return r;
  }
};

class ProbeSocket : public AbstractSocket {
public:
  Trace trace;
  void stateEvent() override {
    trace.record(state_, error(), errorString());
    lsDebug() << LogStream::Color::Cyan
              << "state=" << static_cast<int>(state_)
              << "error=" << static_cast<int>(error())
              << "msg=" << errorString();
  }
};

int rawListen(int &out_fd) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = 0;
  if (::bind(fd, (sockaddr *)&sa, sizeof(sa)) < 0) { ::close(fd); return -1; }
  if (::listen(fd, 1) < 0) { ::close(fd); return -1; }
  socklen_t len = sizeof(sa);
  if (::getsockname(fd, (sockaddr *)&sa, &len) < 0) { ::close(fd); return -1; }
  out_fd = fd;
  return ntohs(sa.sin_port);
}

int pickClosedPort() {
  int fd = -1;
  int p = rawListen(fd);
  if (p < 0) return -1;
  ::close(fd);
  return p;
}

int rawAccept(int listen_fd, int timeout_ms) {
  pollfd pfd{listen_fd, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0) return -1;
  sockaddr_in sa{};
  socklen_t len = sizeof(sa);
  return ::accept(listen_fd, (sockaddr *)&sa, &len);
}

using Done = std::function<void()>;

void test_connect_refused(Done next) {
  lsNotice() << "=== Test 1: connect() to closed port ===";
  int port = pickClosedPort();
  EXPECT(port > 0);

  auto *s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(400, [s, next]() {
    lsNotice() << "  trace: " << s->trace.dump();
    EXPECT(!s->trace.sawConnected);
    EXPECT(s->trace.sawRefused);
    EXPECT(!s->trace.sawClosed);
    EXPECT(s->trace.reachedUnconnected);
    delete s;
    next();
  });
}

void test_graceful_close(Done next) {
  lsNotice() << "=== Test 2: graceful FIN from Active ===";
  int listen_fd = -1;
  int port = rawListen(listen_fd);
  EXPECT(port > 0);

  auto *s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(200, [s, listen_fd, next]() {
    int client_fd = rawAccept(listen_fd, 200);
    EXPECT(client_fd >= 0);
    EXPECT(s->trace.sawActive);

    ::close(client_fd);
    ::close(listen_fd);

    Timer::single(400, [s, next]() {
      lsNotice() << "  trace: " << s->trace.dump();
      EXPECT(!s->trace.sawRefused);
      EXPECT(s->trace.sawClosed);
      delete s;
      next();
    });
  });
}

void test_rst_close(Done next) {
  lsNotice() << "=== Test 3: RST from Active ===";
  int listen_fd = -1;
  int port = rawListen(listen_fd);
  EXPECT(port > 0);

  auto *s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(200, [s, listen_fd, next]() {
    int client_fd = rawAccept(listen_fd, 200);
    EXPECT(client_fd >= 0);
    EXPECT(s->trace.sawActive);

    linger lg{1, 0};
    ::setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    ::close(client_fd);
    ::close(listen_fd);

    Timer::single(400, [s, next]() {
      lsNotice() << "  trace: " << s->trace.dump();
      EXPECT(!s->trace.sawRefused);
      EXPECT(s->trace.sawClosed);
      delete s;
      next();
    });
  });
}

// ============================================================================
// Group E. AbstractThread::Waiter
// ============================================================================

// Waiter::wait() must block until complete() is invoked from the same
// thread, spinning a nested exec() loop internally.
void test_waiter_complete_from_same_thread() {
  Thread t("waiter-basic");
  t.start();

  std::atomic<bool> timerFired{false};
  std::atomic<bool> waitReturned{false};

  t.invoke([&]() {
    Thread::Waiter w;
    Timer::single(30, [&w, &timerFired]() {
      timerFired = true;
      w.complete();
    });
    w.wait();
    waitReturned = true;
  }, true);

  EXPECT(timerFired.load());
  EXPECT(waitReturned.load());
}

// Two waiters in the same thread, completed in order by their own timers.
// Verifies the nested-exec cascade completes correctly and each waiter
// resumes its own caller frame.
void test_waiter_nested() {
  Thread t("waiter-nested");
  t.start();

  std::atomic<int> done{0};
  t.invoke([&]() {
    Thread::Waiter w1;
    Thread::Waiter w2;
    Timer::single(20, [&w1]() { w1.complete(); });
    Timer::single(40, [&w2]() { w2.complete(); });
    w1.wait();
    ++done;
    w2.wait();
    ++done;
  }, true);

  EXPECT(done.load() == 2);
}

// Waiter::complete() called from a different thread must marshal the quit()
// back into the target thread's event loop (see Waiter::complete in
// AbstractThread.cpp).
void test_waiter_complete_from_other_thread() {
  Thread t("waiter-cross");
  t.start();

  std::atomic<Thread::Waiter *> waiterPtr{nullptr};
  std::atomic<bool> waitReturned{false};

  t.invoke([&]() {
    Thread::Waiter w;
    waiterPtr = &w;
    w.wait();
    waiterPtr = nullptr;
    waitReturned = true;
  });

  EXPECT(wait_for([&] { return waiterPtr.load() != nullptr; }, 1000));

  // Trigger completion from the caller's thread. The waiter's own thread
  // is `t`, so complete() must take the cross-thread path.
  waiterPtr.load()->complete();

  EXPECT(wait_for([&] { return waitReturned.load(); }, 2000));
}

}  // namespace

// ============================================================================
// main
// ============================================================================

int main(int, char **) {
  // --- Group A: FunctionConnector -------------------------------------
  test_direct_same_thread();
  test_direct_zero_args();
  test_multiple_direct_subscribers();
  test_argument_copy_semantics_direct();
  test_reentrant_direct();
  test_disconnect_during_emit();
  test_guard_raii();
  test_guard_move();
  test_guard_list();
  test_auto_same_thread();
  test_policy_direct_only();
  test_queued_cross_thread();
  test_sync_cross_thread();
  test_queued_deferred_not_inline();
  test_protected_emitter();
  test_independent_connections();
  test_guard_destroy_during_concurrent_emit();
  test_guard_and_connector_concurrent_destroy_asyncfw();

  // --- Group B: AbstractThread ----------------------------------------
  test_thread_waits_for_running_task();

  // --- Group C: MainThread re-exec ------------------------------------
  // Must be the first user of MainThread::exec() because the fd assertion
  // in test_mainthread_reexec assumes eventfd is created on the first run.
  test_mainthread_reexec();

  // --- Group D: AbstractSocket HUP/ERR --------------------------------
  // Uses MainThread::exec() and Timer::single, so it runs last.
  Timer::single(10, []() {
    test_connect_refused([]() {
      test_graceful_close([]() {
        test_rst_close([]() {
          lsNotice() << "=== AbstractSocket tests done ===";
          MainThread::exit();
        });
      });
    });
  });

  // --- Group E:  AbstractThread::Waiter --------------------------------
  test_waiter_complete_from_same_thread();
  test_waiter_nested();
  test_waiter_complete_from_other_thread();

  MainThread::exec();

  // --- Report ---------------------------------------------------------
  if (g_failed.load()) {
    lsError() << LogStream::Color::Red
              << "core: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "core: " << g_total.load() << " checks passed";
  return 0;
}
