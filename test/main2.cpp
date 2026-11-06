// Tests-main2: standalone DataArrayTcpClient misc suite (ported from
// example/misc/main.cpp).
//
// This file is a SEPARATE binary from tests-main.cpp. It intentionally does
// NOT include Group A/B/C/D/E/F, FileSystemWatcher, WebSocket, io_uring,
// DataArraySocket, or mDNS — those live in tests-main.cpp with its own 209
// checks. Keeping the two suites apart keeps each file reviewable.
//
// Coverage:
//   H1  caller-side timeout on unroutable IP       — waiter must resume only
//                                                    on Active|Unconnected
//   H2  second connect after a timeout             — state-machine check
//   H3  fire-and-forget (timeout = 0)              — non-blocking check
//   H4  rapid accept-close stress (N iterations)   — waiter UAF under ASAN
//   H5  partial frame then close                   — buffer-cleanup check
//   H6  destroy last socket reaps the worker       — pool teardown check
//   H7  pool reuse after teardown                  — clean restart check
//   H8  destroy + quit race                        — known double-delete
//   H9  state transition sequence on accept-close  — order check
//  H10  no auto-reconnect when reconnect == 0      — stay Unconnected
//
// H1/H2 regression note:
//   If those two fail with `state=2 (Connecting)` and elapsed~0ms, the test
//   binary is linked against an OLD libAsyncFw.so that does not contain the
//   `if (state != Active && state != Unconnected) return;` guard in
//   DataArrayTcpClient::connectToHost. Verify with:
//       ldd ./tests-main2 | grep AsyncFw
//   and ensure LD_LIBRARY_PATH / RPATH points to the freshly built .so.
//
// H8 is a REPRODUCER for a real framework bug. removeThread() still returns
// void, so nothing prevents DataArrayAbstractTcp::Thread::destroySocket()
// and AbstractThreadPool::quit() from each scheduling {stop(); delete;} on
// the same Thread*. Expect SIGSEGV at "pure virtual method called" under
// ASAN/TSAN until the ownership-gate fix lands:
//   - add std::atomic<bool> destroying_ to AbstractThreadPool::Thread
//   - destroy(): CAS(false→true) gate before queueing {stop(); delete}
//   - DataArrayAbstractTcp::Thread::destroySocket(): replace inline
//     {stop(); delete _p;} with a single _p->destroy() call
//
// Threading layout:
//   - `poolOwner` runs the pool's event loop; the test client (`dac`) lives
//     there too (dac->thread_ == poolOwner), so client.quit() dispatched
//     from a helper thread synchronously executes _f on poolOwner.
//   - Main executes the tests directly (no MainThread::exec() required):
//     the blocking connectToHost(timeout > 0) path uses Thread::Waiter,
//     which runs a nested exec() on Main and does not need a top-level loop.
//   - H8 runs LAST: it deliberately crashes the process until the framework
//     fix lands. All other checks complete first so the final report is
//     still meaningful.

#include <cstring>
#ifndef _WIN32
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <AsyncFw/ThreadPool>
#include <AsyncFw/Thread>
#include <AsyncFw/Timer>
#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/DataArraySocket>

using namespace AsyncFw;
using Conn = AbstractFunctionConnector::Connection;

namespace {

void log_output(const LogStream::Message &message, uint8_t flags) {
  if ((message.type & 0x07) > 5) return;
  LogStream::console_output(message, flags);
}

// ============================================================================
// Test harness
// ============================================================================

std::atomic<int> g_failed {0};
std::atomic<int> g_total {0};

#define EXPECT(cond)                                                                        \
do {                                                                                      \
      ++g_total;                                                                              \
      if (!(cond)) {                                                                          \
        ++g_failed;                                                                           \
        lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond; \
  }                                                                                       \
} while (0)

#define EXPECT_MSG(cond, msg)                                                                                 \
    do {                                                                                                        \
      ++g_total;                                                                                                \
      if (!(cond)) {                                                                                            \
        ++g_failed;                                                                                             \
        lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond << " — " << (msg); \
  }                                                                                                         \
} while (0)

  int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
  }

// ============================================================================
// Raw POSIX listener driving specific client-side transitions.
// ============================================================================

#ifndef _WIN32
struct RawServer {
  int listen_fd = -1;
  uint16_t port = 0;
  std::atomic<bool> stop {false};
  std::thread thread;

  // mode 1: accept() then immediate close(fd)   → Active then Unconnected.
  // mode 2: partial DataArraySocket frame, then close(fd).
  bool start(int mode) {
    listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return false;
    int one = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (::bind(listen_fd, (sockaddr *)&sa, sizeof(sa)) < 0) return false;
    if (::listen(listen_fd, 8) < 0) return false;
    socklen_t len = sizeof(sa);
    if (::getsockname(listen_fd, (sockaddr *)&sa, &len) < 0) return false;
    port = ntohs(sa.sin_port);
    thread = std::thread([this, mode]() {
      while (!stop.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        timeval tv {0, 50 * 1000};
        if (::select(listen_fd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
          int fd = ::accept(listen_fd, nullptr, nullptr);
          if (fd < 0) continue;
          if (mode == 1) {
            ::close(fd);
          } else if (mode == 2) {
            uint32_t size = 1024 * 1024;
            uint32_t id = 0;
            uint8_t hdr[8];
            std::memcpy(hdr, &size, 4);
            std::memcpy(hdr + 4, &id, 4);
            (void)!::write(fd, hdr, sizeof(hdr));
            (void)!::write(fd, "0123456789abcdef", 16);
            ::close(fd);
          }
        }
      }
    });
    return true;
  }

  ~RawServer() {
    stop = true;
    if (thread.joinable()) thread.join();
    if (listen_fd >= 0) ::close(listen_fd);
  }
};

// ============================================================================
// H1: caller-side timeout on unroutable IP
// ============================================================================
// Post-fix contract: connectToHost(timeout > 0) returns ONLY on a terminal
// state (Active or Unconnected). 192.0.2.1/24 is TEST-NET-1, guaranteed
// black-holed, so the only terminal outcome is Unconnected (via caller-side
// timer → disconnect(), or via ICMP fast-fail → pollEvent close()).
//
// Pre-fix: the stateChanged subscriber in DataArrayTcpClient::connectToHost
// resumed the waiter on the very first transition (Connecting), so the caller
// observed state=2 in ~0ms.
void test_h_dacaller_timeout_unroutable(DataArrayTcpClient &client) {
  lsNotice() << "=== H1: caller-side timeout on unroutable IP ===";
  DataArraySocket *socket = client.createSocket();
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345, 500);
  int64_t elapsed = now_ms() - t0;
  auto st = socket->state();

  EXPECT_MSG(st == AbstractSocket::Unconnected,
             "connectToHost returned in " + std::to_string(elapsed) + "ms, state=" +
                 std::to_string(static_cast<int>(st)) +
                 " (expected 0/Unconnected). Waiter must not resume on Connecting.");
  EXPECT_MSG(elapsed < 1500, "timeout took too long: " + std::to_string(elapsed) + "ms");
  client.destroySocket(socket);
}

// ============================================================================
// H2: second connect after a timeout
// ============================================================================
void test_h_dacaller_timeout_then_reconnect(DataArrayTcpClient &client) {
  lsNotice() << "=== H2: second connect after timeout ===";
  DataArraySocket *socket = client.createSocket();

  client.connectToHost(socket, "192.0.2.1", 12345, 300);
  if (socket->state() != AbstractSocket::Unconnected) {
    EXPECT_MSG(false, "first connect left socket in state " +
                          std::to_string(static_cast<int>(socket->state())) +
                          " (only Unconnected is connectable)");
    client.destroySocket(socket);
    return;
  }

  client.connectToHost(socket, "127.0.0.1", 1, 300);
  auto st = socket->state();
  EXPECT_MSG(st != AbstractSocket::Connecting,
             "second connect stuck in Connecting");
  client.destroySocket(socket);
}

// ============================================================================
// H3: fire-and-forget (timeout = 0)
// ============================================================================
void test_h_dacaller_fire_and_forget(DataArrayTcpClient &client) {
  lsNotice() << "=== H3: fire-and-forget (timeout = 0) ===";
  DataArraySocket *socket = client.createSocket();
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345, 0);
  int64_t elapsed = now_ms() - t0;

  EXPECT_MSG(elapsed <= 50, "fire-and-forget blocked for " + std::to_string(elapsed) + "ms");
  client.destroySocket(socket);
}

// ============================================================================
// H4: rapid accept-close stress
// ============================================================================
// For each iteration:
//   - connect to a raw server that accepts then immediately closes;
//   - connectToHost(2000 ms) must return on Active or Unconnected only;
//   - the socket must settle to Unconnected within ~500 ms;
//   - destroySocket() must not corrupt the worker.
void test_h_dacaller_rapid_accept_close(DataArrayTcpClient &client) {
  lsNotice() << "=== H4: rapid accept-close stress ===";
  RawServer server;
  if (!server.start(1)) { EXPECT_MSG(false, "server start failed"); return; }

  int stuckConnecting = 0;
  int stuckActive = 0;
  int stuckOther = 0;
  constexpr int kIters = 100;

  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    std::atomic<bool> settled {false};
    std::atomic<AbstractSocket::State> lastState {AbstractSocket::State::Unconnected};
    FunctionConnectionGuard g = socket->stateChanged.connect(
        [&settled, &lastState](AbstractSocket::State s) {
      lastState = s;
      if (s == AbstractSocket::State::Unconnected) settled = true;
    });

    client.connectToHost(socket, "127.0.0.1", server.port, 2000);
    auto st = socket->state();

    if (st == AbstractSocket::Connecting) {
      ++stuckConnecting;
      lsError() << "iter" << i << "returned on Connecting";
    } else if (st != AbstractSocket::State::Active && st != AbstractSocket::State::Unconnected) {
      ++stuckOther;
      lsError() << "iter" << i << "unexpected state" << static_cast<int>(st);
    }

    int waited = 0;
    while (!settled && waited < 500) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ++waited;
    }
    if (!settled) {
      auto s = lastState.load();
      if (s == AbstractSocket::State::Active) ++stuckActive;
      else ++stuckOther;
      lsError() << "iter" << i << "did not settle in" << waited
                << "ms, last =" << static_cast<int>(s);
    }

    g = {};
    client.destroySocket(socket);
  }

  EXPECT_MSG(stuckConnecting == 0 && stuckActive == 0 && stuckOther == 0,
             "stuckConnecting=" + std::to_string(stuckConnecting) +
                 " stuckActive=" + std::to_string(stuckActive) +
                 " stuckOther=" + std::to_string(stuckOther) +
                 " of " + std::to_string(kIters));
}

// ============================================================================
// H5: partial frame then close
// ============================================================================
// The waiter contract is "resume on Active OR Unconnected", so on a fast
// accept-close the caller can legitimately observe either state on return:
//   - Active: connectToHost returned as soon as the TCP handshake completed,
//     before the peer FIN is processed by the worker;
//   - Unconnected: FIN arrived and pollEvent() closed the socket before Main
//     read the state.
// Only Connecting would be a bug (waiter resumed on a non-terminal state).
void test_h_dacaller_partial_frame_close(DataArrayTcpClient &client) {
  lsNotice() << "=== H5: partial frame then close ===";
  RawServer server;
  if (!server.start(2)) { EXPECT_MSG(false, "server start failed"); return; }

  DataArraySocket *socket = client.createSocket();
  std::atomic<bool> gotCompleteFrame {false};
  FunctionConnectionGuard g = socket->received.connect(
      [&gotCompleteFrame](const DataArray *, uint32_t) { gotCompleteFrame = true; });

  client.connectToHost(socket, "127.0.0.1", server.port, 2000);
  auto st = socket->state();
  EXPECT_MSG(st == AbstractSocket::State::Active || st == AbstractSocket::State::Unconnected,
             "state=" + std::to_string(static_cast<int>(st)) +
                 " (expected Active or Unconnected; never Connecting)");
  EXPECT_MSG(!gotCompleteFrame.load(),
             "complete frame delivered — scenario void");

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  g = {};
  client.destroySocket(socket);
}

// ============================================================================
// H6: destroying the last socket reaps the worker
// ============================================================================
void test_h_dapool_teardown_on_last_socket(DataArrayTcpClient &client) {
  lsNotice() << "=== H6: pool teardown on last socket ===";
  RawServer server;
  if (!server.start(1)) { EXPECT_MSG(false, "server start failed"); return; }

  DataArraySocket *socket = client.createSocket();
  client.connectToHost(socket, "127.0.0.1", server.port, 500);
  client.destroySocket(socket);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  size_t n = client.threads()->size();
  EXPECT_MSG(n == 0, "threads_=" + std::to_string(n) + " after destroying the last socket");
}

// ============================================================================
// H7: pool reuse after teardown
// ============================================================================
void test_h_dapool_reuse_after_teardown(DataArrayTcpClient &client) {
  lsNotice() << "=== H7: pool reuse after teardown ===";
  RawServer server;
  if (!server.start(1)) { EXPECT_MSG(false, "server start failed"); return; }

  DataArraySocket *s1 = client.createSocket();
  client.connectToHost(s1, "127.0.0.1", server.port, 500);
  client.destroySocket(s1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  DataArraySocket *s2 = client.createSocket();
  EXPECT_MSG(s2 != nullptr, "createSocket returned nullptr after teardown");
  if (!s2) return;
  client.connectToHost(s2, "127.0.0.1", server.port, 500);
  auto st = s2->state();
  EXPECT_MSG(st == AbstractSocket::State::Active || st == AbstractSocket::State::Unconnected,
             "fresh socket in unexpected state " + std::to_string(static_cast<int>(st)));
  client.destroySocket(s2);
}

// ============================================================================
// H8: destroy + quit race  (KNOWN BUG)
// ============================================================================
// destroySocket() (worker-side) queues {stop(); delete _p;} on the pool owner.
// AbstractThreadPool::quit() also iterates pool->threads_ and calls destroy(),
// which likewise queues {stop(); delete;} for the same Thread*. removeThread()
// still returns void, so nothing prevents both paths from scheduling the same
// {stop; delete} — double-free, SIGSEGV at "pure virtual method called".
//
// LIFETIME — CRITICAL:
//   Everything async (helper threads, RawServer, sockets vector) lives in a
//   heap RaceTestCtx. Lambdas capture ctx* by value, not stack references,
//   because the helpers run asynchronously and would otherwise UAF the frame.
//   ctx is deleted at the end of the test (after both helpers joined and
//   the poolOwner barrier drained all pending {stop; delete} tasks).
//
// This test WILL crash the process until the fix lands. It runs last.
struct RaceTestCtx {
  RawServer server;
  std::vector<DataArraySocket *> sockets;
  std::atomic<bool> destroyerDone {false};
  std::atomic<bool> quitterDone {false};
};

void test_h_dadestroy_races_quit(DataArrayTcpClient &client, AbstractThread *poolOwner) {
  lsNotice() << "=== H8: destroy vs quit race ===";
  lsNotice() << "KNOWN BUG: expect double-{stop;delete;} under ASAN/TSAN until "
                "the ownership-gate fix lands";

  auto *ctx = new RaceTestCtx;
  if (!ctx->server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    delete ctx;
    return;
  }

  constexpr int kSockets = 4;
  for (int i = 0; i < kSockets; ++i) {
    DataArraySocket *s = client.createSocket();
    if (!s) break;
    client.connectToHost(s, "127.0.0.1", ctx->server.port, 500);
    ctx->sockets.push_back(s);
  }
  if (ctx->sockets.empty()) {
    EXPECT_MSG(false, "no sockets created for race test");
    delete ctx;
    return;
  }

  std::thread destroyer([ctx, &client]() {
    for (auto *s : ctx->sockets) client.destroySocket(s);
    ctx->destroyerDone.store(true);
  });

  std::thread quitter([ctx, &client]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    client.quit();
    ctx->quitterDone.store(true);
  });

  int waited = 0;
  while (waited < 5000 && (!ctx->destroyerDone.load() || !ctx->quitterDone.load())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    waited += 10;
  }
  destroyer.join();
  quitter.join();

  EXPECT_MSG(ctx->destroyerDone.load() && ctx->quitterDone.load(),
             "helpers finished within 5s (destroyer=" +
                 std::to_string(ctx->destroyerDone.load()) + " quitter=" +
                 std::to_string(ctx->quitterDone.load()) + ")");

  // Barrier: drain poolOwner's queue so any pending {stop; delete} tasks
  // (from destroy()'s queued lambdas) run before we check. Without this,
  // the double-delete would fire during a later test.
  if (poolOwner) poolOwner->invoke([]() {}, true);

  // Reaching this point without crashing implies the race is fixed.
  size_t n = client.threads()->size();
  EXPECT_MSG(n == 0,
             "SURVIVED barrier without crashing; threads_=" + std::to_string(n));

  delete ctx;
}

// ============================================================================
// H9: state transition sequence on accept-close
// ============================================================================
void test_h_dastate_sequence(DataArrayTcpClient &client) {
  lsNotice() << "=== H9: state sequence on accept-close ===";
  RawServer server;
  if (!server.start(1)) { EXPECT_MSG(false, "server start failed"); return; }

  int badSequences = 0;
  constexpr int kIters = 20;

  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    std::mutex m;
    std::vector<AbstractSocket::State> seq;
    std::atomic<bool> settled {false};

    FunctionConnectionGuard g = socket->stateChanged.connect(
        [&m, &seq, &settled](AbstractSocket::State s) {
      std::lock_guard<std::mutex> l(m);
      seq.push_back(s);
      if (s == AbstractSocket::State::Unconnected) settled = true;
    });

    client.connectToHost(socket, "127.0.0.1", server.port, 500);

    int waited = 0;
    while (!settled && waited < 500) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ++waited;
    }
    g = {};

    std::lock_guard<std::mutex> l(m);
    bool sawActive = false;
    bool sawUnconnected = false;
    bool badOrder = false;
    for (auto s : seq) {
      if (s == AbstractSocket::State::Active) {
        if (sawActive) badOrder = true;
        sawActive = true;
      } else if (s == AbstractSocket::State::Unconnected) {
        if (sawUnconnected) badOrder = true;
        sawUnconnected = true;
      } else if (sawActive) {
        badOrder = true;
      }
    }
    if (!sawActive || !sawUnconnected || badOrder) {
      ++badSequences;
      std::string dump;
      for (auto s : seq) { dump += std::to_string(static_cast<int>(s)) + " "; }
      lsError() << "iter" << i << "bad sequence:[" << dump << "]";
    }

    client.destroySocket(socket);
  }

  EXPECT_MSG(badSequences == 0,
             std::to_string(badSequences) + " of " + std::to_string(kIters) +
                 " iterations had a bad state sequence");
}

// ============================================================================
// H10: no auto-reconnect when reconnectTimeout == 0
// ============================================================================
void test_h_dano_reconnect_with_zero_timeout(DataArrayTcpClient &client) {
  lsNotice() << "=== H10: no auto-reconnect (reconnectTimeout == 0) ===";
  DataArraySocket *socket = client.createSocket();

  std::atomic<int> transitionCount {0};
  FunctionConnectionGuard g = socket->stateChanged.connect(
      [&transitionCount](AbstractSocket::State) { ++transitionCount; });

  client.connectToHost(socket, "127.0.0.1", 1, 500);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto st2 = socket->state();
  int n = transitionCount.load();
  g = {};

  EXPECT_MSG(st2 == AbstractSocket::Unconnected,
             "state=" + std::to_string(static_cast<int>(st2)) +
                 " (expected Unconnected after refused connect)");
  EXPECT_MSG(n <= 3,
             "too many state transitions (" + std::to_string(n) +
                 ") — reconnect likely active");
  client.destroySocket(socket);
}

#endif  // !_WIN32

}  // namespace

// ============================================================================
// main
// ============================================================================

int main(int, char **) {
  LogStream::setCompleted(&log_output);

#ifdef _WIN32
  lsNotice() << "tests-main2: skipped on Windows (no POSIX raw sockets)";
  return 0;
#else
  Thread poolOwner("pool-owner");
  poolOwner.start();
  poolOwner.invoke([]() { Instance<ThreadPool>::create("MainTests2Pool"); }, true);

  DataArrayTcpClient *dac = nullptr;
  poolOwner.invoke([&dac]() {
    dac = new DataArrayTcpClient("DataArrayTcpMisc");
    dac->setConnectTimeout(2000);
    dac->setReconnectTimeout(0);
  }, true);

  test_h_dacaller_timeout_unroutable(*dac);
  test_h_dacaller_timeout_then_reconnect(*dac);
  test_h_dacaller_fire_and_forget(*dac);
  test_h_dacaller_rapid_accept_close(*dac);
  test_h_dacaller_partial_frame_close(*dac);
  test_h_dapool_teardown_on_last_socket(*dac);
  test_h_dapool_reuse_after_teardown(*dac);
  test_h_dastate_sequence(*dac);
  test_h_dano_reconnect_with_zero_timeout(*dac);

  // H8 runs last: it crashes the process until the ownership-gate fix lands.
  test_h_dadestroy_races_quit(*dac, &poolOwner);

  poolOwner.invoke([&dac]() { delete dac; dac = nullptr; }, true);
  poolOwner.invoke([]() {
    if (ThreadPool *pool = ThreadPool::instance()) pool->quit();
  }, true);

  if (g_failed.load()) {
    lsError() << LogStream::Color::Red << "tests-main2: " << g_failed.load() << '/'
              << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "tests-main2: " << g_total.load() << " checks passed";
  return 0;
#endif
}
