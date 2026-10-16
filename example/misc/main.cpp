#if 0
  #include <AsyncFw/MainThread>
  #include <AsyncFw/LogStream>
  #include <AsyncFw/DataArray>

int main(int argc, char *argv[]) {
  AsyncFw::Thread::current()->invoke([]() { AsyncFw::MainThread::exit(); });

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application " << ret;
  return ret;
}
#endif

#if 1
  #include <atomic>
  #include <chrono>
  #include <memory>
  #include <thread>

  #include <AsyncFw/FunctionConnector>
  #include <AsyncFw/MainThread>
  #include <AsyncFw/Thread>
  #include <AsyncFw/Timer>
  #include <AsyncFw/LogStream>

using namespace AsyncFw;
using Conn = AbstractFunctionConnector::Connection;

// ---------------------------------------------------------------------------
// Tiny assertion harness — no external dependency.
// g_failed is returned from main so CI can detect the failure.
// ---------------------------------------------------------------------------
namespace {
std::atomic<int> g_failed = 0;
std::atomic<int> g_total = 0;

  #define EXPECT(cond)                                                                        \
    do {                                                                                      \
      ++g_total;                                                                              \
      if (!(cond)) {                                                                          \
        ++g_failed;                                                                           \
        lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond; \
      }                                                                                       \
    } while (0)

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
std::atomic<int> Tracker::copies {0};
std::atomic<int> Tracker::moves {0};

template <typename F>
bool wait_for(F pred, int ms = 2000) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

// ---------------------------------------------------------------------------
// 1. Direct delivery, same thread
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 2. Multicast copy semantics — no move-steal from first subscriber.
//    Regression target: invocable.hpp warning.
// ---------------------------------------------------------------------------
void test_argument_copy_semantics_direct() {
  Tracker::copies = 0;
  Tracker::moves = 0;
  FunctionConnector<Tracker> fc;
  int seen1 = -1, seen2 = -1;
  fc.connect<Conn::Direct>([&](Tracker t) { seen1 = *t.p; });
  fc.connect<Conn::Direct>([&](Tracker t) { seen2 = *t.p; });

  Tracker original {7};
  int copies_before = Tracker::copies.load();
  fc(original);

  EXPECT(seen1 == 7);        // first subscriber saw intact data
  EXPECT(seen2 == 7);        // second subscriber saw intact data
  EXPECT(*original.p == 7);  // sender object untouched
  EXPECT(Tracker::copies.load() >= copies_before + 2);
}

// ---------------------------------------------------------------------------
// 3. Reentrancy — nested emit from inside a Direct subscriber.
//    Regression target: hangs forever if operator() holds the mutex while
//    invoking slots (see `#if 0` variant).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 4. Disconnect-during-emit — already-snapshotted subscribers still receive
//    the current event (documented behaviour).
// ---------------------------------------------------------------------------
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
  EXPECT(b == 1);  // b was already in the emission snapshot
  fc(0);
  EXPECT(a == 2);
  EXPECT(b == 1);  // b is gone
}

// ---------------------------------------------------------------------------
// 5. Guard RAII / move / list
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 6. Auto policy — same thread resolves to Direct (inline).
// ---------------------------------------------------------------------------
void test_auto_same_thread() {
  FunctionConnector<int> fc;
  int got = 0;
  fc.connect([&](int v) { got = v; });
  fc(11);
  EXPECT(got == 11);
}

// ---------------------------------------------------------------------------
// 7. Policy<DirectOnly> — subscribe without type, run inline.
// ---------------------------------------------------------------------------
void test_policy_direct_only() {
  FunctionConnector<int>::Policy<AbstractFunctionConnector::DirectOnly> fc;
  auto caller_id = std::this_thread::get_id();
  bool same = false;
  fc.connect([&](int) { same = (std::this_thread::get_id() == caller_id); });
  fc(0);
  EXPECT(same);
}

// ---------------------------------------------------------------------------
// 8. Queued cross-thread — deferred into target thread's event loop.
// ---------------------------------------------------------------------------
void test_queued_cross_thread() {
  Thread target("test-queued");
  target.start();

  FunctionConnector<int> fc;
  std::atomic<int> got {0};
  std::atomic<bool> done {false};

  // Connection::thread_ captured at connect() time — must be `target`.
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

// ---------------------------------------------------------------------------
// 9. Sync cross-thread — sender blocks until target slot returns.
// ---------------------------------------------------------------------------
void test_sync_cross_thread() {
  Thread target("test-sync");
  target.start();

  FunctionConnector<int> fc;
  std::atomic<int> got {0};

  target.invoke([&]() { fc.connect<Conn::Sync>([&](int v) { got = v; }); }, true);

  fc(123);
  EXPECT(got == 123);  // no polling: Sync guarantees completion
}

// ---------------------------------------------------------------------------
// 10. Queued same-thread is deferred, not inline.
// ---------------------------------------------------------------------------
void test_queued_deferred_not_inline() {
  Thread target("test-deferred");
  target.start();

  bool seen_inline = false;
  std::atomic<int> got {0};
  std::atomic<bool> ready {false};

  target.invoke([&]() {
    FunctionConnector<int> fc;
    fc.connect<Conn::Queued>([&](int v) {
      got = v;
      seen_inline = true;
    });
    fc(7);
    EXPECT(!seen_inline);  // Queued must NOT run inline
    Timer::single(20, [&]() { ready = true; });
  }, true);

  EXPECT(wait_for([&] { return ready.load(); }));
  EXPECT(got == 7);
  EXPECT(seen_inline);
}

// ---------------------------------------------------------------------------
// 11. Protected signal — emission only through the owner class.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 12. Two independent connections to the same callable.
// ---------------------------------------------------------------------------
void test_independent_connections() {
  FunctionConnector<int> fc;
  int n = 0;
  auto callable = [&](int) { ++n; };
  auto &c1 = fc.connect<Conn::Direct>(callable);
  auto &c2 = fc.connect<Conn::Direct>(callable);

  fc(0);
  EXPECT(n == 2);  // two independent connections

  {
    FunctionConnectionGuard g(c1);  // takes ownership, c1 still in list
    fc(0);
    EXPECT(n == 4);  // both fire while guard alive
  }  // g destroyed → c1 removed

  fc(0);
  EXPECT(n == 5);  // only c2 remains
  (void)c2;
}

void test_guard_destroy_during_concurrent_emit() {
  FunctionConnector<int> fc;
  std::atomic<int>  received{0};
  std::atomic<bool> stop{false};

  // Connection создаётся в главном потоке, оборачивается в heap-guard.
  FunctionConnectionGuard *guard = new FunctionConnectionGuard(
      fc.connect<Conn::Direct>([&](int v) {
    received.fetch_add(v, std::memory_order_relaxed);
    std::this_thread::yield();
  }));

  // Эмиттер в отдельном потоке.
  std::thread emitter([&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      fc(1);
      std::this_thread::yield();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  // Уничтожаем guard, пока эмиттер работает. Здесь TSan должен либо
  // молчать, либо указать на гонку по Connection::guard_.
  delete guard;

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop = true;
  emitter.join();

  lsNotice() << "test_guard_destroy_during_concurrent_emit: received =" << received.load();
  EXPECT(received.load() > 0);
}
// ---------------------------------------------------------------------------
// Guard уничтожается в одном AsyncFw::Thread, коннектор — в другом.
// std::thread не нужен: используем AsyncFw::Thread и его invoke().
//
// ПОРЯДОК ОБЪЯВЛЕНИЯ ВАЖЕН:
//   Лямбды, отправленные через invoke(), выполняются асинхронно и читают
//   ready/go/done по ссылке. C++ разрушает локали в порядке, обратном
//   объявлению. Если объявить ready/go после tA/tB, то на выходе из scope
//   они будут разрушены РАНЬШЕ ~Thread, а лямбды в этот момент ещё бегут —
//   ASan ловит это как stack-use-after-scope.
//
//   Поэтому ready/go/done объявлены до tA/tB: они доживают до момента,
//   когда оба потока гарантированно завершены.
//
// ПОЧЕМУ done:
//   ~Thread блокирует вызывающий поток до фактического завершения exec(),
//   но полагаться только на это хрупко — неявный контракт. done явно
//   фиксирует «обе лямбды закончили читать go/ready», до выхода из scope.
// ---------------------------------------------------------------------------
void test_guard_and_connector_concurrent_destroy_asyncfw() {
  for (int i = 0; i < 200; ++i) {
    std::atomic<int>  ready{0};    // сколько лямбд дошли до spin-wait
    std::atomic<bool> go{false};   // сигнал «отпускаем обе лямбды»
    std::atomic<int>  done{0};     // сколько лямбд полностью вернулись

    auto *fc = new FunctionConnector<int>;
    auto *g  = new FunctionConnectionGuard(
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
      done.fetch_add(1);           // последний визит к общим данным
    });
    tB.invoke([&]() {
      ready.fetch_add(1);
      while (!go.load()) std::this_thread::yield();
      delete fc;
      fc = nullptr;
      done.fetch_add(1);           // последний визит к общим данным
    });

    // Обе лямбды стартовали и висят в spin-wait
    while (ready.load() < 2) std::this_thread::yield();

    // Отпускаем обе лямбды — стартует конкурирующее разрушение
    go = true;

    // Дожидаемся полного завершения лямбд до выхода из scope.
    // После этого ни один поток больше не трогает ready/go/done.
    while (done.load() < 2) std::this_thread::yield();

    // Здесь вызываются ~tB и ~tA (в этом порядке — обратном объявлению).
    // К этому моменту лямбды мертвы, ждать особо нечего, но ~Thread
    // всё равно корректно дожидается exec().
  }
  EXPECT(true);
}
}  // namespace

int main(int, char **) {
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

  if (g_failed) {
    lsError() << LogStream::Color::Red << "FunctionConnectorTest:" << g_failed.load() << '/' << g_total.load() << "checks failed";
    return 1;
  }
  lsNotice() << "FunctionConnectorTest:" << g_total.load() << "checks passed";
  return 0;
}
#endif

#if 0
  #include <atomic>

  #include <AsyncFw/MainThread>
  #include <AsyncFw/Timer>
  #include <AsyncFw/LogStream>

  #ifndef _WIN32
    #include <dirent.h>
  #endif

using namespace AsyncFw;

namespace {

int open_fd_count() {
  #ifndef _WIN32
  int n = 0;
  DIR *d = opendir("/proc/self/fd");
  if (!d) return -1;
  while (struct dirent *e = readdir(d)) {
    if (e->d_name[0] == '.') continue;  // skip '.' and '..'
    ++n;
  }
  closedir(d);
  return n;
  #else
  return 0;
  #endif
}

int g_failed = 0;
int g_total = 0;

  #define EXPECT(cond)                                                                        \
    do {                                                                                      \
      ++g_total;                                                                              \
      if (!(cond)) {                                                                          \
        ++g_failed;                                                                           \
        lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond; \
      }                                                                                       \
    } while (0)

}  // namespace

int main(int argc, char **argv) {
  #ifdef USE_QAPPLICATION
  QCoreApplication app(argc, argv);
  #endif

  // Track lifetime events of the main thread across both exec() runs.
  std::atomic<int> started_count {0};
  std::atomic<int> finished_count {0};
  Thread::current()->started.connect<AbstractFunctionConnector::Connection::Direct>([&]() { ++started_count; });
  Thread::current()->finished.connect<AbstractFunctionConnector::Connection::Direct>([&]() { ++finished_count; });

  int fds_before = open_fd_count();

  // -------- first exec -----------------------------------------------
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

  // -------- second exec ----------------------------------------------
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

  // eventfd_ (or exit_pipe) is created exactly once, on the first `started`.
  EXPECT(fds_after_1 == fds_before + 1);
  // Second exec re-emits `started` but the setup guard makes it idempotent,
  // so no additional descriptors are opened.
  EXPECT(fds_after_2 == fds_after_1);

  if (g_failed) {
    lsError() << LogStream::Color::Red << "MainThreadReExecTest:" << g_failed << '/' << g_total << " checks failed";
    return 1;
  }
  lsNotice() << "MainThreadReExecTest:" << g_total << " checks passed";
  return 0;
}
#endif

#if 0
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

#if 0
  #include <AsyncFw/MainThread>
  #include <AsyncFw/LogStream>
  #include <AsyncFw/AbstractSocket>
  #include <AsyncFw/Timer>

  #include <poll.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>

  #include <functional>
  #include <string>
  #include <vector>

using namespace AsyncFw;

namespace {

int g_failed = 0;
int g_total = 0;

  #define EXPECT(cond)                                                                                \
    do {                                                                                              \
      ++g_total;                                                                                      \
      if (!(cond)) {                                                                                  \
        ++g_failed;                                                                                   \
        lsError() << LogStream::Color::Red << "FAIL " << __FILE__ << ':' << __LINE__ << ' ' << #cond; \
      } else {                                                                                        \
        lsDebug() << LogStream::Color::Green << "ok   " << __LINE__ << ' ' << #cond;                  \
      }                                                                                               \
    } while (0)

struct Trace {
  std::vector<int> states;
  std::vector<int> errors;
  std::vector<std::string> strings;
  bool sawConnected = false;
  bool sawActive = false;
  bool sawRefused = false;
  bool sawClosed = false;
  bool reachedUnconnected = false;

  void record(int state, int err, const std::string& es) {
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

// ---------- raw helper ---------------------------

int rawListen(int& out_fd) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = 0;
  if (::bind(fd, (sockaddr*)&sa, sizeof(sa)) < 0) { ::close(fd); return -1; }
  if (::listen(fd, 1) < 0) { ::close(fd); return -1; }
  socklen_t len = sizeof(sa);
  if (::getsockname(fd, (sockaddr*)&sa, &len) < 0) { ::close(fd); return -1; }
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
  return ::accept(listen_fd, (sockaddr*)&sa, &len);
}

// ---------- scenarios ----------------------------

using Done = std::function<void()>;

void test_connect_refused(Done next) {
  lsNotice() << "=== Test 1: connect() to closed port ===";
  int port = pickClosedPort();
  EXPECT(port > 0);

  auto* s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(400, [s, next]() {
    lsNotice() << "  trace: " << s->trace.dump();
    // Problem 1: must NOT emit Connected for a refused connect.
    EXPECT(!s->trace.sawConnected);
    // Problem 2: must report Refused (not Closed).
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

  auto* s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(200, [s, listen_fd, next]() {
    int client_fd = rawAccept(listen_fd, 200);
    EXPECT(client_fd >= 0);
    EXPECT(s->trace.sawActive);

    ::close(client_fd);   // graceful FIN
    ::close(listen_fd);

    Timer::single(400, [s, next]() {
      lsNotice() << "  trace: " << s->trace.dump();
      // Problem 3: graceful close must NOT be classified as Refused.
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

  auto* s = new ProbeSocket();
  s->connect("127.0.0.1", port);

  Timer::single(200, [s, listen_fd, next]() {
    int client_fd = rawAccept(listen_fd, 200);
    EXPECT(client_fd >= 0);
    EXPECT(s->trace.sawActive);

    linger lg{1, 0};      // SO_LINGER(0) → close() sends RST
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

}  // namespace

int main(int argc, char* argv[]) {
  lsNotice() << "AbstractSocket HUP/ERR error classification test";

  Timer::single(10, []() {
    test_connect_refused([]() {
      test_graceful_close([]() {
        test_rst_close([]() {
          lsNotice() << "=== Done ===";
          if (g_failed) lsError() << LogStream::Color::Red << "FAILED " << g_failed << '/' << g_total;
          else          lsNotice() << LogStream::Color::Green << "PASSED " << g_total;
          MainThread::exit(g_failed ? 1 : 0);
        });
      });
    });
  });

  return MainThread::exec();
}
#endif
