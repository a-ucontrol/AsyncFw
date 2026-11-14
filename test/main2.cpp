// Tests-main2: DataArrayTcp client + server suite.
//
// Client-side contract:
//   - connectToHost() is async and never blocks the caller.
//   - Failed connects settle to Unconnected; socket stays reconnectable.
//   - accept-close and partial-frame-close settle to Unconnected.
//   - Destroying the last socket reaps the worker thread.
//   - reconnectTimeout == 0 disables auto-reconnect.
//   - quit() from any thread synchronously tears the pool down on owner.
//
// Server-side contract:
//   - DataArrayTcpServer listens, accepts, round-trips a frame, and
//     tears down cleanly (listener closed, workers self-reap, no leaks).
//   - Destroying a DataArrayTcpServer while a live socket is held by a
//     worker MUST NOT leave the worker running after ~disconnected (the
//     UAF window closed by ~DataArrayTcpServer::quit() plus the trailing
//     barrier in AbstractThreadPool::quit()).
//
// sockets() contract (new public API on DataArrayAbstractTcp):
//   - int sockets()                -> count of live sockets across workers
//   - template <T> sockets(T f)    -> visitor on each worker, count
//   - visitor runs on the worker thread (sockets_ is only stable there)
//   - sockets() takes ownership of the internal Invocable wrapper
//   - socket pointers passed to the visitor are valid only inside the call
//   - sockets() is safe to call from any thread: it re-enters on the
//     pool owner, which serializes with start()/setId() of newly created
//     workers (fixes the data race on AbstractThread::id()).
//
// State-observation notes
// -----------------------
// DataArraySocket::stateChanged and ::received are `protected` -- they are
// connection targets of DataArrayAbstractTcp / DataArrayTcpClient, not of
// application code. The pool-level connectors re-emit the same events:
//   - DataArrayTcpClient::connectionStateChanged(const DataArraySocket *)
//   - DataArrayTcpClient::received(const DataArraySocket *, const DataArray *,
//                                  uint32_t)
//   - DataArrayTcpServer::received(...) -- same shape
// Tests subscribe to the pool connectors and filter by socket pointer.
//
// DataArraySocket::stateEvent() suppresses the Connecting state -- it calls
// stateChanged() only for Connected, Active, and Unconnected. On a FAILED
// connect there is no Connected/Active phase, so the only stateChanged a
// subscriber ever observes is exactly one Unconnected. A "left/back" pattern
// (expect a non-Unconnected first, then Unconnected) is therefore wrong for
// failed connects -- `left` never fires and the test hangs on timeout.
//
// The robust signal for "connect attempt settled" is: the count of
// stateChanged(Unconnected) fires increased since just before the connect
// was issued. All cycle tests below observe that.
//
// Note about connectionStateChanged's argument: it delivers only the socket
// pointer, not the new State value. Subscribers that need the state must
// read socket->state() (public, atomic).
//
// Threading layout:
//   - all DataArrayTcpClient / DataArrayTcpServer instances are created on
//     `poolOwner`, so their `thread_` is poolOwner. Every deferred
//     socketStateChanged / received delivery lands on poolOwner's queue.
//   - poolOwner's exec() runs for the whole test, so deferred tasks are
//     drained as soon as they are queued.
//   - destructing a client / server from poolOwner (via poolOwner.invoke)
//     drains prior tasks before the destructor runs, so the framework's
//     FunctionConnector members are still alive when late tasks execute.
//     Destroying from a non-owner thread without a barrier would be a
//     use-after-free of the connector (see MiscExample history).

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
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <AsyncFw/ThreadPool>
#include <AsyncFw/Thread>
#include <AsyncFw/LogStream>
#include <AsyncFw/DataArray>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/DataArrayTcpServer>
#include <AsyncFw/DataArraySocket>

using namespace AsyncFw;

namespace {

void log_output(const LogStream::Message &message, uint8_t flags) {
  if ((message.type & 0x07) > 5) return;
  LogStream::console_output(message, flags);
}

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

template <typename F>
bool wait_for(F pred, int ms = 2000) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

// Waits until the counter grows past `before`. See file header: on a failed
// connect this is the only reliable "settled" signal, because stateChanged
// never fires for the intermediate Connecting state.
bool waitForUnconnectedFires(std::atomic<int> &counter, int before, int ms = 2000) {
  return wait_for([&] { return counter.load() > before; }, ms);
}

int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------------------------------------------------------------------------
// AddressLedger — test-side diagnostic for heap address reuse across
// iterations. Not part of the framework; purely observational.
// ---------------------------------------------------------------------------
struct AddressLedger {
  std::mutex m;
  std::map<const void *, int> created;
  std::map<const void *, int> destroyed;

  void onCreate(const void *p, int iter) {
    std::lock_guard<std::mutex> l(m);
    auto it = created.find(p);
    if (it != created.end() && !destroyed.count(p)) { lsWarning() << "ADDRESS REUSE on create: " << p << " created in iter " << it->second << ", reused in iter " << iter << " while previous still live"; }
    created[p] = iter;
    destroyed.erase(p);
  }
  void onDestroy(const void *p, int iter) {
    std::lock_guard<std::mutex> l(m);
    if (destroyed.count(p)) { lsWarning() << "DOUBLE DESTROY observed for " << p << " at iter " << iter << " (previously " << destroyed[p] << ")"; }
    destroyed[p] = iter;
  }
  ~AddressLedger() {
    std::lock_guard<std::mutex> l(m);
    for (auto &kv : created) {
      if (!destroyed.count(kv.first)) { lsWarning() << "SOCKET LEAK: " << kv.first << " created in iter " << kv.second << " never destroyed"; }
    }
  }
};
AddressLedger g_ledger;

// Move-only functor for the sockets() move-only probe.
struct CountingFunctor {
  std::atomic<int> *counter;

  explicit CountingFunctor(std::atomic<int> *c) : counter(c) {}

  CountingFunctor(const CountingFunctor &) = delete;
  CountingFunctor &operator=(const CountingFunctor &) = delete;
  CountingFunctor(CountingFunctor &&) = default;
  CountingFunctor &operator=(CountingFunctor &&) = default;

  void operator()(const DataArraySocket *) const { counter->fetch_add(1); }
};

#ifndef _WIN32

// Raw POSIX listener used to drive specific client-side transitions.
//   mode 1: accept() then immediate close(fd).
//   mode 2: partial DataArraySocket frame, then close(fd).
struct RawServer {
  int listen_fd = -1;
  uint16_t port = 0;
  std::atomic<bool> stop {false};
  std::thread thread;

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
// connectToHost() is async and never blocks the caller.
// ============================================================================
void test_da_connect_is_async(DataArrayTcpClient &client) {
  lsNotice() << "=== connectToHost() is async ===";
  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 0);
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345);
  int64_t elapsed = now_ms() - t0;
  EXPECT_MSG(elapsed <= 50, "connectToHost blocked for " + std::to_string(elapsed) + "ms");
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 0);
}

// ============================================================================
// Failed connect settles to Unconnected; socket stays reconnectable.
// ============================================================================
void test_da_reconnect_after_failed_connect(DataArrayTcpClient &client) {
  lsNotice() << "=== reconnect after failed connect ===";
  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 100);

  std::atomic<int> unconnCount {0};
  std::atomic<int> foreign {0};
  FunctionConnectionGuard g = client.connectionStateChanged.connect([&, socket](const DataArraySocket *s) {
    lsTrace() << "reconnect-cycle delivery s=" << s << " state=" << static_cast<int>(s->state()) << " expect=" << socket;
    if (s != socket) {
      ++foreign;
      lsWarning() << "reconnect-cycle FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << socket << ")";
      return;
    }
    if (s->state() == AbstractSocket::State::Unconnected) unconnCount.fetch_add(1);
  });

  auto runCycle = [&](const char *label, const char *addr, uint16_t port) -> bool {
    const int before = unconnCount.load();
    client.connectToHost(socket, addr, port);
    const bool ok = waitForUnconnectedFires(unconnCount, before, 2000);
    if (!ok) {
      lsError() << label << " did not settle;"
                << " state=" << static_cast<int>(socket->state()) << " unconnCount(before=" << before << ", now=" << unconnCount.load() << ")"
                << " foreign=" << foreign.load();
    }
    return ok;
  };

  EXPECT_MSG(runCycle("first (unroutable)", "192.0.2.1", 12345), "first connect did not settle");
  EXPECT_MSG(runCycle("second (refused)", "127.0.0.1", 1), "second connect did not settle");

  AbstractThread::current()->invoke([]() {}, true);
  g = {};
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 100);
}

// ============================================================================
// accept-close stress.
// ============================================================================
void test_da_rapid_accept_close(DataArrayTcpClient &client) {
  lsNotice() << "=== rapid accept-close stress ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  int stuck = 0;
  int foreignTotal = 0;
  constexpr int kIters = 300;

  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    g_ledger.onCreate(socket, i);

    // State живёт в куче. Поздняя эмиссия из _il-снимка в operator()
    // держит свою копию shared_ptr -> состояние не умирает вместе с
    // итерацией, и stack-reuse (TSAN: "Previous write ... Location is
    // stack of thread T2") больше не создаёт гонку.
    struct State {
      std::atomic<int> unconnCount {0};
      std::atomic<int> foreign {0};
      const DataArraySocket *expected = nullptr;
    };
    auto state = std::make_shared<State>();
    state->expected = socket;

    FunctionConnectionGuard g = client.connectionStateChanged.connect([state, i](const DataArraySocket *s) {
      if (s != state->expected) {
        state->foreign.fetch_add(1, std::memory_order_relaxed);
        lsWarning() << "rapid iter" << i << " FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << state->expected << ")";
        return;
      }
      if (s->state() == AbstractSocket::State::Unconnected) state->unconnCount.fetch_add(1, std::memory_order_relaxed);
    });

    client.connectToHost(socket, "127.0.0.1", server.port);

    const bool settled = waitForUnconnectedFires(state->unconnCount, 0, 500);

    if (!settled) {
      ++stuck;
      lsError() << "rapid iter" << i << " did not settle in 500ms,"
                << " selfState=" << static_cast<int>(socket->state()) << " unconnCount=" << state->unconnCount.load() << " foreign=" << state->foreign.load() << " socket=" << socket;
    }
    foreignTotal += state->foreign.load();

    g = {};
    client.destroySocket(socket);
    g_ledger.onDestroy(socket, i);
  }

  lsNotice() << "rapid accept-close summary: stuck=" << stuck << "/" << kIters << " foreign deliveries=" << foreignTotal;
  EXPECT_MSG(stuck == 0, "stuck=" + std::to_string(stuck) + "/" + std::to_string(kIters) + " foreign=" + std::to_string(foreignTotal));
}

// ============================================================================
// Partial frame then close.
// ============================================================================
void test_da_partial_frame_close(DataArrayTcpClient &client) {
  lsNotice() << "=== partial frame then close ===";
  RawServer server;
  if (!server.start(2)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 200);
  std::atomic<int> unconnCount {0};
  std::atomic<bool> gotCompleteFrame {false};
  std::atomic<int> foreign {0};

  FunctionConnectionGuardList guards;
  guards += client.connectionStateChanged.connect([&, socket](const DataArraySocket *s) {
    lsTrace() << "partial-frame delivery s=" << s << " state=" << static_cast<int>(s->state()) << " expect=" << socket;
    if (s != socket) {
      ++foreign;
      lsWarning() << "partial-frame FOREIGN s=" << s << " state=" << static_cast<int>(s->state());
      return;
    }
    if (s->state() == AbstractSocket::State::Unconnected) unconnCount.fetch_add(1);
  });
  guards += client.received.connect([&, socket](const DataArraySocket *s, const DataArray *, uint32_t) {
    if (s != socket) return;
    gotCompleteFrame.store(true);
  });

  client.connectToHost(socket, "127.0.0.1", server.port);

  const bool settled = waitForUnconnectedFires(unconnCount, 0, 2000);
  AbstractThread::current()->invoke([]() {}, true);

  EXPECT_MSG(settled,
             "socket did not settle within 2000ms;"
             " state=" +
                 std::to_string(static_cast<int>(socket->state())) + " unconnCount=" + std::to_string(unconnCount.load()) + " foreign=" + std::to_string(foreign.load()));
  EXPECT_MSG(!gotCompleteFrame.load(), "complete frame delivered — scenario void");

  guards.clear();
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 200);
}

// ============================================================================
// Destroying the last socket reaps the worker thread.
// ============================================================================
void test_da_pool_teardown_on_last_socket(DataArrayTcpClient &client) {
  lsNotice() << "=== pool teardown on last socket ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 300);
  client.connectToHost(socket, "127.0.0.1", server.port);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 300);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  size_t n = client.threads()->size();
  EXPECT_MSG(n == 0, "threads_=" + std::to_string(n) + " after destroying the last socket");
}

// ============================================================================
// Pool is reusable after full teardown.
// ============================================================================
void test_da_pool_reuse_after_teardown(DataArrayTcpClient &client) {
  lsNotice() << "=== pool reuse after teardown ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  DataArraySocket *s1 = client.createSocket();
  g_ledger.onCreate(s1, 400);
  client.connectToHost(s1, "127.0.0.1", server.port);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.destroySocket(s1);
  g_ledger.onDestroy(s1, 400);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  DataArraySocket *s2 = client.createSocket();
  g_ledger.onCreate(s2, 401);
  EXPECT_MSG(s2 != nullptr, "createSocket returned nullptr after teardown");
  if (!s2) return;

  std::atomic<int> unconnCount {0};
  std::atomic<int> foreign {0};
  FunctionConnectionGuard g = client.connectionStateChanged.connect([&, s2](const DataArraySocket *s) {
    if (s != s2) {
      ++foreign;
      lsWarning() << "pool-reuse FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << s2 << ")";
      return;
    }
    if (s->state() == AbstractSocket::State::Unconnected) unconnCount.fetch_add(1);
  });

  client.connectToHost(s2, "127.0.0.1", server.port);
  const bool ok = waitForUnconnectedFires(unconnCount, 0, 2000);
  AbstractThread::current()->invoke([]() {}, true);

  EXPECT_MSG(ok,
             "fresh socket did not settle;"
             " state=" +
                 std::to_string(static_cast<int>(s2->state())) + " unconnCount=" + std::to_string(unconnCount.load()) + " foreign=" + std::to_string(foreign.load()));
  g = {};
  client.destroySocket(s2);
  g_ledger.onDestroy(s2, 401);
}

// ============================================================================
// state sequence on accept-close (diagnostic, 500 iterations).
// ============================================================================
void test_da_state_sequence(DataArrayTcpClient &client) {
  lsNotice() << "=== state sequence on accept-close ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  int badIterations = 0;
  int foreignTotal = 0;
  int noDelivery = 0;
  int resurrected = 0;
  constexpr int kIters = 500;

  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    g_ledger.onCreate(socket, i);

    struct State {
      std::atomic<int> unconnCount {0};
      std::atomic<int> foreign {0};
      std::atomic<int> selfNonUnconn {0};
      const DataArraySocket *expected = nullptr;
    };
    auto state = std::make_shared<State>();
    state->expected = socket;

    FunctionConnectionGuard g = client.connectionStateChanged.connect([state, i](const DataArraySocket *s) {
      const int st = static_cast<int>(s->state());
      lsTrace() << "state-seq iter" << i << " delivery s=" << s << " state=" << st << " expect=" << state->expected;
      if (s != state->expected) {
        state->foreign.fetch_add(1, std::memory_order_relaxed);
        lsWarning() << "state-seq iter" << i << " FOREIGN s=" << s << " state=" << st << " (expected " << state->expected << ")";
        return;
      }
      if (s->state() == AbstractSocket::State::Unconnected) {
        state->unconnCount.fetch_add(1, std::memory_order_relaxed);
      } else {
        state->selfNonUnconn.fetch_add(1, std::memory_order_relaxed);
        lsDebug() << "state-seq iter" << i << " self delivery with non-Unconnected state=" << st;
      }
    });

    client.connectToHost(socket, "127.0.0.1", server.port);

    const bool settled = waitForUnconnectedFires(state->unconnCount, 0, 500);

    const int fc = state->foreign.load();
    const int uc = state->unconnCount.load();
    const bool bad = !settled || uc == 0 || socket->state() != AbstractSocket::State::Unconnected;

    if (bad) {
      ++badIterations;
      foreignTotal += fc;
      if (uc == 0) ++noDelivery;
      if (fc == 0 && uc > 0 && socket->state() != AbstractSocket::State::Unconnected) ++resurrected;

      lsError() << "state-seq iter" << i << " settled=" << settled << " unconnCount=" << uc << " foreign=" << fc << " selfNonUnconn=" << state->selfNonUnconn.load() << " finalState=" << static_cast<int>(socket->state()) << " socket=" << socket;

      if (fc > 0) {
        lsError() << "state-seq iter" << i << " => FOREIGN delivery observed";
      } else if (uc == 0) {
        lsError() << "state-seq iter" << i << " => self Unconnected never delivered";
      } else {
        lsError() << "state-seq iter" << i << " => self Unconnected delivered but state is not Unconnected";
      }
    }

    g = {};
    client.destroySocket(socket);
    g_ledger.onDestroy(socket, i);
  }

  lsNotice() << "state sequence summary: bad=" << badIterations << "/" << kIters << " foreign=" << foreignTotal << " noDelivery=" << noDelivery << " resurrected=" << resurrected;

  EXPECT_MSG(badIterations == 0, std::to_string(badIterations) + " of " + std::to_string(kIters) + " iterations did not settle in Unconnected (foreign=" + std::to_string(foreignTotal) + " noDelivery=" + std::to_string(noDelivery) + " resurrected=" + std::to_string(resurrected) + ")");
}

// ============================================================================
// reconnectTimeout == 0 disables auto-reconnect.
// ============================================================================
void test_da_no_reconnect_with_zero_timeout(DataArrayTcpClient &client) {
  lsNotice() << "=== no auto-reconnect (reconnectTimeout == 0) ===";
  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 500);

  std::atomic<int> transitionCount {0};
  FunctionConnectionGuard g = client.connectionStateChanged.connect([&, socket](const DataArraySocket *s) {
    if (s != socket) {
      lsWarning() << "no-reconnect FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << socket << ")";
      return;
    }
    ++transitionCount;
  });

  client.connectToHost(socket, "127.0.0.1", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  AbstractThread::current()->invoke([]() {}, true);

  const int n = transitionCount.load();
  g = {};

  EXPECT_MSG(socket->state() == AbstractSocket::State::Unconnected, "socket did not settle to Unconnected; state=" + std::to_string(static_cast<int>(socket->state())));
  EXPECT_MSG(n <= 3, "too many state transitions (" + std::to_string(n) + ") — reconnect likely active");
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 500);
}

// ============================================================================
// DataArrayTcpServer: listen / accept / roundtrip.
// ============================================================================
void test_tcp_server_roundtrip(AbstractThread *serverOwner, DataArrayTcpClient &client) {
  lsNotice() << "=== DataArrayTcpServer roundtrip ===";
  const uint16_t port = 18999;

  std::atomic<DataArrayTcpServer *> serverPtr {nullptr};
  serverOwner->invoke([&serverPtr]() { serverPtr = new DataArrayTcpServer("DataArrayTcpServerMisc"); }, true);
  DataArrayTcpServer *server = serverPtr.load();
  EXPECT_MSG(server != nullptr, "server allocation failed");
  if (!server) return;

  DataArraySocket *cs = client.createSocket();
  g_ledger.onCreate(cs, 600);
  EXPECT_MSG(cs != nullptr, "client.createSocket() failed");
  if (!cs) {
    serverOwner->invoke([&]() { delete server; }, true);
    return;
  }

  std::atomic<bool> active {false};
  std::atomic<int> clientRecv {0};
  std::atomic<uint32_t> clientRecvId {0xFFFFFFFFu};
  std::atomic<int> serverRecv {0};
  std::atomic<uint32_t> serverRecvId {0xFFFFFFFFu};
  std::atomic<int> foreign {0};
  std::mutex serverMutex;
  std::string serverPayload;

  FunctionConnectionGuardList guards;
  guards += client.connectionStateChanged.connect([&, cs](const DataArraySocket *s) {
    if (s != cs) {
      ++foreign;
      lsWarning() << "roundtrip FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << cs << ")";
      return;
    }
    if (cs->state() == AbstractSocket::State::Active) active.store(true);
  });
  guards += client.received.connect([&, cs](const DataArraySocket *s, const DataArray *, uint32_t id) {
    if (s != cs) return;
    clientRecvId = id;
    ++clientRecv;
  });
  guards += server->received.connect([&](const DataArraySocket *s, const DataArray *d, uint32_t id) {
    {
      std::lock_guard<std::mutex> l(serverMutex);
      serverPayload.assign(d->begin(), d->end());
    }
    serverRecvId = id;
    ++serverRecv;
    const_cast<DataArraySocket *>(s)->transmit(*d, id + 100);
  });

  std::atomic<bool> listenOk {false};
  serverOwner->invoke([&]() { listenOk = server->listen("127.0.0.1", port); }, true);
  EXPECT_MSG(listenOk.load(), "server.listen(" + std::to_string(port) + ") failed");

  if (listenOk.load()) {
    client.connectToHost(cs, "127.0.0.1", port);
    const bool gotActive = wait_for([&] { return active.load(); }, 3000);
    EXPECT_MSG(gotActive, "client did not reach Active; state=" + std::to_string(static_cast<int>(cs->state())));

    if (gotActive) {
      cs->transmit(DataArray("hello"), 42);

      const bool gotServerRecv = wait_for([&] { return serverRecv.load() >= 1; }, 2000);
      EXPECT_MSG(gotServerRecv, "server did not receive within 2s");

      if (gotServerRecv) {
        std::lock_guard<std::mutex> l(serverMutex);
        EXPECT_MSG(serverPayload == "hello", "server payload='" + serverPayload + "'");
      }
      EXPECT_MSG(serverRecvId.load() == 42u, "server id=" + std::to_string(serverRecvId.load()) + " (expected 42)");

      const bool gotClientRecv = wait_for([&] { return clientRecvId.load() == 142u; }, 2000);
      EXPECT_MSG(gotClientRecv,
                 "client did not receive expected echo (id=142) within 2s;"
                 " clientRecv=" +
                     std::to_string(clientRecv.load()) + " clientRecvId=" + std::to_string(clientRecvId.load()));
    }
  }

  guards.clear();
  client.destroySocket(cs);
  g_ledger.onDestroy(cs, 600);
  serverOwner->invoke([&]() { server->close(); }, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  serverOwner->invoke([&]() { delete server; }, true);
}

// ============================================================================
// quit() from a foreign thread synchronously tears the pool down.
// ============================================================================
void test_da_quit_from_foreign_thread(DataArrayTcpClient &client) {
  lsNotice() << "=== quit() from foreign thread ===";

  DataArraySocket *socket = client.createSocket();
  g_ledger.onCreate(socket, 700);
  EXPECT_MSG(socket != nullptr, "createSocket failed");
  EXPECT_MSG(client.threads()->size() > 0, "pool has no workers before quit");

  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 700);

  std::thread foreign([&client]() { client.quit(); });
  foreign.join();

  const size_t after = client.threads()->size();
  EXPECT_MSG(after == 0, "threads_=" + std::to_string(after) + " after quit() from foreign thread");
}

// ============================================================================
// ===============  sockets() API probes (DataArrayAbstractTcp)  ==============
// ============================================================================
//
// Each probe creates its own DataArrayTcpClient on `poolOwner` so that
// deferred socketStateChanged / received deliveries land on poolOwner's
// running exec loop. The client is destroyed from poolOwner as well, so
// the destructor drains prior tasks before tearing down the
// FunctionConnector members.

// ----------------------------------------------------------------------------
// Probe A: sockets() returns the number of live sockets.
// ----------------------------------------------------------------------------
void test_sockets_count(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() count ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsCountProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 6; ++i) {
    DataArraySocket *s = client->createSocket();
    if (!s) {
      EXPECT_MSG(false, "createSocket failed");
      break;
    }
    sockets.push_back(s);
  }

  int total = client->sockets();
  lsInfo() << "total:" << total;
  EXPECT(total == 6);

  for (DataArraySocket *s : sockets) client->destroySocket(s);
  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe B: sockets(lambda) visits each socket exactly once.
// ----------------------------------------------------------------------------
void test_sockets_visitor_visits_all(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets(lambda) visits all ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsVisitorProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 5; ++i) {
    DataArraySocket *s = client->createSocket();
    if (!s) break;
    sockets.push_back(s);
  }

  std::atomic<int> visited {0};
  int total = client->sockets([&visited](const DataArraySocket *) { visited.fetch_add(1, std::memory_order_relaxed); });

  lsInfo() << "total:" << total << "visited:" << visited.load();
  EXPECT(total == 5);
  EXPECT(visited.load() == 5);

  for (DataArraySocket *s : sockets) client->destroySocket(s);
  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe C: visitor runs on the worker thread, not on the caller.
// ----------------------------------------------------------------------------
void test_sockets_visitor_runs_on_worker(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets(lambda) runs on worker ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsThreadProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 8; ++i) {
    DataArraySocket *s = client->createSocket();
    if (s) sockets.push_back(s);
  }

  // Collect worker ids up front. threads() returns a LockData; copy ids
  // and release the lock before sockets() to avoid holding pool->mutex
  // while sockets() itself locks it.
  std::vector<std::thread::id> workerIds;
  {
    auto locked = client->threads();
    for (AbstractThreadPool::Thread *t : *locked) workerIds.push_back(t->id());
  }

  std::mutex m;
  std::vector<std::thread::id> observed;

  int total = client->sockets([&m, &observed](const DataArraySocket *) {
    std::lock_guard<std::mutex> l(m);
    observed.push_back(std::this_thread::get_id());
  });

  bool mismatch = false;
  {
    std::lock_guard<std::mutex> l(m);
    for (std::thread::id oid : observed) {
      bool found = false;
      for (std::thread::id wid : workerIds) {
        if (wid == oid) {
          found = true;
          break;
        }
      }
      if (!found) {
        mismatch = true;
        break;
      }
    }
  }

  lsInfo() << "total:" << total << "observed:" << observed.size() << "workers:" << workerIds.size() << "mismatch:" << mismatch;
  EXPECT(total == 8);
  EXPECT(observed.size() == 8);
  EXPECT(!mismatch);

  for (DataArraySocket *s : sockets) client->destroySocket(s);
  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe D: sockets() on an empty pool returns 0; visitor is never invoked.
// ----------------------------------------------------------------------------
void test_sockets_empty_pool(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() on empty pool ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsEmptyProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::atomic<int> visited {0};
  int total = client->sockets([&visited](const DataArraySocket *) { visited.fetch_add(1); });

  lsInfo() << "total:" << total << "visited:" << visited.load();
  EXPECT(total == 0);
  EXPECT(visited.load() == 0);

  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe E: sockets() sees the current live set, not a stale snapshot.
// ----------------------------------------------------------------------------
void test_sockets_sees_current_state(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() sees current state ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsStateProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 4; ++i) {
    DataArraySocket *s = client->createSocket();
    if (s) sockets.push_back(s);
  }

  int before = client->sockets();
  EXPECT(before == 4);

  if (!sockets.empty()) {
    client->destroySocket(sockets.back());
    sockets.pop_back();
    // Let the async destroy settle on poolOwner before the next sockets().
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  int after = client->sockets();
  lsInfo() << "before:" << before << "after:" << after;
  EXPECT(after == 3);

  for (DataArraySocket *s : sockets) client->destroySocket(s);
  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe F: sockets() is per-pool, not global.
// ----------------------------------------------------------------------------
void test_sockets_per_pool(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() is per-pool ===";

  DataArrayTcpClient *a = nullptr;
  DataArrayTcpClient *b = nullptr;
  poolOwner->invoke([&]() {
    a = new DataArrayTcpClient("PoolA");
    b = new DataArrayTcpClient("PoolB");
  }, true);
  if (!a || !b) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> aSockets;
  std::vector<DataArraySocket *> bSockets;
  for (int i = 0; i < 3; ++i) {
    DataArraySocket *s = a->createSocket();
    if (s) aSockets.push_back(s);
  }
  for (int i = 0; i < 5; ++i) {
    DataArraySocket *s = b->createSocket();
    if (s) bSockets.push_back(s);
  }

  int ca = a->sockets();
  int cb = b->sockets();
  lsInfo() << "poolA:" << ca << "poolB:" << cb;
  EXPECT(ca == 3);
  EXPECT(cb == 5);

  for (DataArraySocket *s : aSockets) a->destroySocket(s);
  for (DataArraySocket *s : bSockets) b->destroySocket(s);
  poolOwner->invoke([&]() {
    delete a;
    delete b;
  }, true);
}

// ----------------------------------------------------------------------------
// Probe G: sockets() after destroying every socket returns 0.
// Also demonstrates sockets() as a drain barrier: destroySocket is
// asynchronous on the pool owner, so the pool reports non-zero for a
// short while after the last destroySocket call.
// ----------------------------------------------------------------------------
void test_sockets_after_all_destroyed(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() after all destroyed ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsDrainProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 4; ++i) {
    DataArraySocket *s = client->createSocket();
    if (s) sockets.push_back(s);
  }

  EXPECT(client->sockets() == 4);
  for (DataArraySocket *s : sockets) client->destroySocket(s);

  int attempts = 0;
  while (client->sockets() != 0 && attempts++ < 1000) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  int after = client->sockets();
  lsInfo() << "after drain:" << after << "attempts:" << attempts;
  EXPECT(after == 0);

  poolOwner->invoke([&]() { delete client; }, true);
}

// ----------------------------------------------------------------------------
// Probe H: sockets(lambda) on DataArrayTcpServer — API is symmetric.
// ----------------------------------------------------------------------------
void test_sockets_server_side(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets() on DataArrayTcpServer ===";

  const uint16_t port = 18099;
  DataArrayTcpServer *server = nullptr;
  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() {
    server = new DataArrayTcpServer("SocketsServerProbe");
    client = new DataArrayTcpClient("SocketsServerClient");
  }, true);
  if (!server || !client) {
    EXPECT_MSG(false, "server/client alloc failed");
    if (client) poolOwner->invoke([&]() { delete client; }, true);
    if (server) poolOwner->invoke([&]() { delete server; }, true);
    return;
  }

  // IMPORTANT: every server->sockets() call goes through poolOwner.
  //
  // incomingConnection() runs on poolOwner and does, in this order:
  //   1) new Thread(name, this)   -> appendThread() under pool->mutex
  //      (the Thread is now visible in threads_)
  //   2) thread->start()          -> blocks until setId() completes in
  //      the new thread
  //
  // There is a window between (1) and the end of (2) where a Thread is
  // visible in threads_ but its id() is still the default-constructed
  // value, being concurrently written by setId() on the new thread. If
  // sockets() runs on a foreign thread during this window, invoke() ->
  // id() reads that field concurrently with setId()'s write.
  //
  // AbstractThread::id() is safe *by contract* only after start() has
  // returned. Serializing sockets() on poolOwner closes the window:
  // start() blocks poolOwner until setId() is done, so any sockets()
  // task queued on poolOwner runs strictly after the new worker's id
  // is stable.
  std::atomic<bool> listenOk {false};
  poolOwner->invoke([&]() { listenOk = server->listen("127.0.0.1", port); }, true);
  EXPECT_MSG(listenOk.load(), "server.listen(" + std::to_string(port) + ") failed");

  int initial = -1;
  poolOwner->invoke([&]() { initial = server->sockets(); }, true);
  EXPECT(initial == 0);

  if (listenOk.load()) {
    DataArraySocket *cs = client->createSocket();
    if (cs) {
      client->connectToHost(cs, "127.0.0.1", port);

      int attempts = 0;
      int n = 0;
      while (attempts++ < 3000) {
        poolOwner->invoke([&]() { n = server->sockets(); }, true);
        if (n == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      lsInfo() << "server sockets:" << n;
      EXPECT_MSG(n == 1, "server accepted no sockets; n=" + std::to_string(n));

      std::atomic<int> visited {0};
      int visitedCount = 0;
      poolOwner->invoke([&]() { visitedCount = server->sockets([&visited](const DataArraySocket *) { visited.fetch_add(1); }); }, true);
      EXPECT_MSG(visitedCount == 1, "server sockets(lambda) total=" + std::to_string(visitedCount));
      EXPECT_MSG(visited.load() == 1, "server sockets(lambda) visited=" + std::to_string(visited.load()));

      client->destroySocket(cs);
    }
  }

  poolOwner->invoke([&]() { server->close(); }, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  poolOwner->invoke([&]() { delete client; }, true);
  poolOwner->invoke([&]() { delete server; }, true);
}
// ----------------------------------------------------------------------------
// Probe I: sockets(lambda) accepts a move-only functor.
//
// The template overload forwards the callable by std::forward into
// Invocable<...>::Function, which constructs F by move. A functor with
// deleted copy ctor and defaulted move ctor must therefore work.
// ----------------------------------------------------------------------------
void test_sockets_move_only_functor(AbstractThread *poolOwner) {
  lsNotice() << "=== sockets(move-only functor) ===";

  DataArrayTcpClient *client = nullptr;
  poolOwner->invoke([&]() { client = new DataArrayTcpClient("SocketsFunctorProbe"); }, true);
  if (!client) {
    EXPECT_MSG(false, "client alloc failed");
    return;
  }

  std::vector<DataArraySocket *> sockets;
  for (int i = 0; i < 3; ++i) {
    DataArraySocket *s = client->createSocket();
    if (s) sockets.push_back(s);
  }

  std::atomic<int> visited {0};
  int total = client->sockets(CountingFunctor {&visited});

  lsInfo() << "total:" << total << "visited:" << visited.load();
  EXPECT(total == 3);
  EXPECT(visited.load() == 3);

  for (DataArraySocket *s : sockets) client->destroySocket(s);
  poolOwner->invoke([&]() { delete client; }, true);
}

// ============================================================================
// ===============  UAF window: server destroyed with live sockets  ==========
// ============================================================================
//
// Reproducer for the UAF that used to fire when a live server-side socket
// received FIN after ~DataArrayTcpServer had already destroyed
// DataArrayTcpServer::disconnected but before the worker was stopped.
//
// Background. Before ~DataArrayTcpServer::quit() existed, the destruction
// order was:
//
//   ~DataArrayTcpServer()
//     (implicit) member destruction in reverse declaration order:
//        ~listener
//        ~disconnected            ← FunctionConnector dead here
//        ~DataArrayAbstractTcp
//        ~AbstractThreadPool      ← quit() finally stops the workers
//
// Between ~disconnected and the workers actually stopping there was a
// window in which a live server-side socket, still polled by its worker,
// could receive FIN, call stateEvent(Unconnected) -> stateChanged ->
// server()->disconnected(tcpSocket), and hit a destroyed mutex. TSan
// reported "use of an invalid mutex" or "heap-use-after-free".
//
// The fix in the current tree:
//   - ~DataArrayTcpServer() calls quit() in the body (before members go away);
//   - AbstractThreadPool::quit() now ends with `thread_->invoke([], true)`,
//     so stop-tasks actually run before quit() returns, and the workers
//     are dead before ~disconnected / ~AbstractTcp run.
//
// This test forces the window with a tight loop: create server + client,
// wait for accept, delete server, immediately send FIN, repeat. If any
// iteration reaches ~disconnected while the worker is still alive, TSan
// reports the UAF and the test will be flagged by the sanitizer build.
//
// Why it must run on a dedicated owner. `poolOwner` must have its own
// exec() running so that (a) incomingConnection()'s synchronous start()
// completes, and (b) the trailing barrier in ~AbstractThreadPool::quit()
// can drain the stop-tasks. Using the global Main thread would make the
// test order-sensitive.
//
// Why sockets() is invoked via poolOwner.invoke(..., true). The pool's
// sockets() walks threads() and, for each worker, calls invoke() which
// reads id(). AbstractThread::id() is only safe after start() has
// returned (see Probe H comments). Serializing sockets() on the owner
// closes the id()/setId() race window that the earlier version of this
// reproducer was hitting.
void test_da_server_destroy_uaf_window(AbstractThread *poolOwner) {
  lsNotice() << "=== server destroyed with live socket (UAF window) ===";
  constexpr uint16_t kPort = 18997;
  constexpr int kIterations = 300;

  int foreignTotal = 0;
  int stuck = 0;

  for (int i = 0; i < kIterations; ++i) {
    // ---- Create server on owner -----------------------------------------
    DataArrayTcpServer *server = nullptr;
    poolOwner->invoke([&]() {
      server = new DataArrayTcpServer("uaf-server-probe");
      server->listen("127.0.0.1", kPort);
    }, true);
    if (!server) {
      EXPECT_MSG(false, "server allocation failed");
      return;
    }

    // ---- Create client on caller (this thread) --------------------------
    DataArrayTcpClient client("uaf-client-probe");
    DataArraySocket *cs = client.createSocket();
    g_ledger.onCreate(cs, 10000 + i);

    std::atomic<int> foreign {0};
    FunctionConnectionGuard g = client.connectionStateChanged.connect([&, cs](const DataArraySocket *s) {
      if (s != cs) ++foreign;
    });

    client.connectToHost(cs, "127.0.0.1", kPort);

    // ---- Wait for accept: sockets() must run on the owner ---------------
    // Serializing on poolOwner closes the id()/setId() race window.
    std::atomic<int> n {0};
    for (int tries = 0; tries < 2000; ++tries) {
      poolOwner->invoke([&]() { n = server->sockets(); }, true);
      if (n.load() > 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (n.load() == 0) {
      ++stuck;
      lsError() << "uaf-window iter" << i << ": server did not accept client";
      client.destroySocket(cs);
      g_ledger.onDestroy(cs, 10000 + i);
      g = {};
      poolOwner->invoke([&]() { delete server; }, true);
      continue;
    }

    // ---- Destroy the server BEFORE the client FIN arrives ---------------
    // The worker is still alive right after delete returns; ~disconnected
    // is now destroyed but ~AbstractThreadPool::quit()'s trailing barrier
    // guarantees the worker is stopped before ~disconnected runs — as long
    // as ~DataArrayTcpServer::quit() was called from the body, which the
    // current tree does. This test exists to catch regressions of that
    // invariant.
    poolOwner->invoke([&]() { delete server; }, true);
    server = nullptr;

    // ---- Immediately send FIN from the client --------------------------
    client.destroySocket(cs);
    g_ledger.onDestroy(cs, 10000 + i);

    // Let the owner drain stop-tasks for this iteration before the next.
    poolOwner->invoke([&]() {}, true);

    foreignTotal += foreign.load();
    g = {};
  }

  lsNotice() << "uaf-window summary: iterations=" << kIterations << " stuck=" << stuck << " foreign=" << foreignTotal;
  EXPECT_MSG(stuck == 0, "server never accepted in " + std::to_string(stuck) + "/" + std::to_string(kIterations) + " iterations");
  EXPECT_MSG(foreignTotal == 0, "foreign connectionStateChanged deliveries: " + std::to_string(foreignTotal));
}

#endif  // !_WIN32

}  // namespace

int main(int, char **) {
  LogStream::setCompleted(&log_output);

#ifdef _WIN32
  lsNotice() << "tests-main2: skipped on Windows (no POSIX raw sockets)";
  return 0;
#else
  Thread poolOwner("pool-owner");
  poolOwner.start();
  poolOwner.invoke([]() { Instance<ThreadPool>::create("MainTests2Pool"); }, true);

  Thread testMain("test-main");
  testMain.start();

  testMain.invoke([&poolOwner]() {
    DataArrayTcpClient *dac = nullptr;
    poolOwner.invoke([&dac]() {
      dac = new DataArrayTcpClient("DataArrayTcpMisc");
      dac->setConnectTimeout(500);
      dac->setReconnectTimeout(0);
    }, true);

    // ---- Cycle / teardown tests (shared client `dac`) ----------------
    test_da_connect_is_async(*dac);
    test_da_reconnect_after_failed_connect(*dac);
    test_da_rapid_accept_close(*dac);
    test_da_partial_frame_close(*dac);
    test_da_pool_teardown_on_last_socket(*dac);
    test_da_pool_reuse_after_teardown(*dac);
    test_da_state_sequence(*dac);
    test_da_no_reconnect_with_zero_timeout(*dac);
    test_tcp_server_roundtrip(&poolOwner, *dac);

    poolOwner.invoke([&dac]() {
      delete dac;
      dac = nullptr;
    }, true);

    // ---- sockets() API probes (each creates its own client on owner) --
    test_sockets_count(&poolOwner);
    test_sockets_visitor_visits_all(&poolOwner);
    test_sockets_visitor_runs_on_worker(&poolOwner);
    test_sockets_empty_pool(&poolOwner);
    test_sockets_sees_current_state(&poolOwner);
    test_sockets_per_pool(&poolOwner);
    test_sockets_after_all_destroyed(&poolOwner);
    test_sockets_server_side(&poolOwner);
    test_sockets_move_only_functor(&poolOwner);

    // ---- UAF window: server destroyed with live socket -----------------
    // Must run last among the server tests: it tears the workers down by
    // deleting the server, then immediately FINs the client. Kept after
    // all sockets() probes so it doesn't pollute their shared state.
    test_da_server_destroy_uaf_window(&poolOwner);

    // ---- quit() from a foreign thread (destroys the pool) ------------
    // Must run last: it tears down `dac`'s pool entirely. Since we
    // already deleted `dac`, this test uses its own client.
    DataArrayTcpClient *quitClient = nullptr;
    poolOwner.invoke([&]() {
      quitClient = new DataArrayTcpClient("DataArrayTcpQuitMisc");
      quitClient->setReconnectTimeout(0);
    }, true);
    if (quitClient) {
      test_da_quit_from_foreign_thread(*quitClient);
      poolOwner.invoke([&]() { delete quitClient; }, true);
    }

    poolOwner.invoke([]() {
      if (ThreadPool *pool = ThreadPool::instance()) pool->quit();
    }, true);
  }, true);

  if (g_failed.load()) {
    lsError() << LogStream::Color::Red << "tests-main2: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "tests-main2: " << g_total.load() << " checks passed";
  return 0;
#endif
}
