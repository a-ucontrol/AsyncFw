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
// Address-reuse hazard (diagnostic rationale)
// -------------------------------------------
// connectionStateChanged() is delivered to Main asynchronously (via
// thread_->invoke from DataArrayTcpClient::socketStateChanged). Meanwhile
// DataArrayTcpClient::destroySocket() returns before the worker thread
// actually deletes the socket (destroy is asynchronous on the pool owner).
// The allocator can therefore hand the *same heap address* to the next
// iteration's createSocket() while a late Unconnected from the previous
// socket is still queued on Main. A subscriber filter of `s != socket`
// matches by address, so the late event is mis-attributed to the new
// socket and unconnCount is incremented spuriously.
//
// The AddressLedger below records every create/destroy in the test process
// so the log explicitly shows such collisions. All accept-close tests also
// flush the Main queue with invoke([](){}, true) before deciding, which
// guarantees any late delivery from THIS socket has already landed when
// we read socket->state().

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
//
// Rationale: destroySocket() returns before the socket is actually deleted
// on the worker (socket->destroy() is async on the pool owner). The
// allocator may hand the same address to the next createSocket() while a
// late connectionStateChanged delivery from the previous socket is still
// queued on Main. Logging these events makes such collisions explicit.
// ---------------------------------------------------------------------------
struct AddressLedger {
  std::mutex m;
  std::map<const void *, int> created;    // addr -> iteration
  std::map<const void *, int> destroyed;  // addr -> iteration

  void onCreate(const void *p, int iter) {
    std::lock_guard<std::mutex> l(m);
    auto it = created.find(p);
    if (it != created.end() && !destroyed.count(p)) { lsWarning() << "ADDRESS REUSE on create: " << p << " created in iter " << it->second << ", reused in iter " << iter << " while previous still live (destroy deferred by async pool teardown)"; }
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
//
// Both attempts share one counter subscriber. Each connect attempt
// increments the counter exactly once (on its Unconnected fire).
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

  AbstractThread::current()->invoke([]() {}, true);  // flush Main queue
  g = {};
  client.destroySocket(socket);
  g_ledger.onDestroy(socket, 100);
}

// ============================================================================
// accept-close stress: each iteration observes exactly one Unconnected fire
// after connectToHost within 500 ms.
//
// Iteration count raised to 300: the address-reuse race is rare (allocator
// dependent), so a small loop may never hit it. Diagnostics log every
// delivery and the live socket set so the failing case is self-describing.
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

    std::atomic<int> unconnCount {0};
    std::atomic<int> foreign {0};
    FunctionConnectionGuard g = client.connectionStateChanged.connect([&, socket, i](const DataArraySocket *s) {
      if (s != socket) {
        ++foreign;
        lsWarning() << "rapid iter" << i << " FOREIGN s=" << s << " state=" << static_cast<int>(s->state()) << " (expected " << socket << ")";
        return;
      }
      if (s->state() == AbstractSocket::State::Unconnected) unconnCount.fetch_add(1);
    });

    client.connectToHost(socket, "127.0.0.1", server.port);

    const bool settled = waitForUnconnectedFires(unconnCount, 0, 500);

    // Snapshot pool sockets BEFORE and AFTER the flush to see whether a
    // previous iteration's socket is still alive at this point (which is
    // what enables address reuse).
    std::vector<DataArraySocket *> liveBefore;
    client.sockets(&liveBefore);

    AbstractThread::current()->invoke([]() {}, true);  // flush Main queue

    std::vector<DataArraySocket *> liveAfter;
    client.sockets(&liveAfter);

    if (!settled) {
      ++stuck;
      lsError() << "rapid iter" << i << " did not settle in 500ms,"
                << " selfState=" << static_cast<int>(socket->state()) << " unconnCount=" << unconnCount.load() << " foreign=" << foreign.load() << " liveBefore=" << liveBefore.size() << " liveAfter=" << liveAfter.size() << " socket=" << socket;
    }
    foreignTotal += foreign.load();

    g = {};
    client.destroySocket(socket);
    g_ledger.onDestroy(socket, i);
  }

  lsNotice() << "rapid accept-close summary: stuck=" << stuck << "/" << kIters << " foreign deliveries=" << foreignTotal;
  EXPECT_MSG(stuck == 0, "stuck=" + std::to_string(stuck) + "/" + std::to_string(kIters) + " foreign=" + std::to_string(foreignTotal));
}

// ============================================================================
// Partial frame then close: settle to Unconnected, no complete frame delivered.
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
  AbstractThread::current()->invoke([]() {}, true);  // flush Main queue

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
// accept-close must terminate in Unconnected for every iteration.
//
// Diagnostic version with heavy logging. Iteration count raised to 500 to
// catch the rare address-reuse race. Per iteration:
//   - logs every connectionStateChanged delivery with s, state, expect
//   - logs the live socket set before/after Main queue flush
//   - flushes the Main queue BEFORE deciding, so any late delivery from
//     THIS socket has already landed when we read socket->state()
//   - categorises the failure (foreign delivery vs. no delivery vs.
//     resurrected state) and prints it
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

    std::atomic<int> unconnCount {0};
    std::atomic<int> foreign {0};
    std::atomic<int> selfNonUnconn {0};

    FunctionConnectionGuard g = client.connectionStateChanged.connect([&, socket, i](const DataArraySocket *s) {
      const int st = static_cast<int>(s->state());
      lsTrace() << "state-seq iter" << i << " delivery s=" << s << " state=" << st << " expect=" << socket;
      if (s != socket) {
        ++foreign;
        lsWarning() << "state-seq iter" << i << " FOREIGN s=" << s << " state=" << st << " (expected " << socket << ")";
        return;
      }
      if (s->state() == AbstractSocket::State::Unconnected) {
        unconnCount.fetch_add(1);
      } else {
        ++selfNonUnconn;
        lsDebug() << "state-seq iter" << i << " self delivery with non-Unconnected state=" << st;
      }
    });

    client.connectToHost(socket, "127.0.0.1", server.port);

    const bool settled = waitForUnconnectedFires(unconnCount, 0, 500);

    // Snapshot pool state right after settle, BEFORE any flush.
    std::vector<DataArraySocket *> liveBefore;
    client.sockets(&liveBefore);
    lsTrace() << "state-seq iter" << i << " settled=" << settled << " unconnCount=" << unconnCount.load() << " foreign=" << foreign.load() << " selfNonUnconn=" << selfNonUnconn.load() << " selfState=" << static_cast<int>(socket->state()) << " liveBefore=" << liveBefore.size() << " socket=" << socket;

    // Flush pending connectionStateChanged deliveries on Main so that any
    // late Unconnected from THIS socket lands before we decide.
    AbstractThread::current()->invoke([]() {}, true);

    // Snapshot again after flush.
    std::vector<DataArraySocket *> liveAfter;
    client.sockets(&liveAfter);
    lsTrace() << "state-seq iter" << i << " after-flush selfState=" << static_cast<int>(socket->state()) << " unconnCount=" << unconnCount.load() << " foreign=" << foreign.load() << " liveAfter=" << liveAfter.size();

    const int fc = foreign.load();
    const int uc = unconnCount.load();
    const bool bad = !settled || uc == 0 || socket->state() != AbstractSocket::State::Unconnected;

    if (bad) {
      ++badIterations;
      foreignTotal += fc;
      if (uc == 0) ++noDelivery;
      if (fc == 0 && uc > 0 && socket->state() != AbstractSocket::State::Unconnected) ++resurrected;

      lsError() << "state-seq iter" << i << " settled=" << settled << " unconnCount=" << uc << " foreign=" << fc << " selfNonUnconn=" << selfNonUnconn.load() << " finalState=" << static_cast<int>(socket->state()) << " liveBefore=" << liveBefore.size() << " liveAfter=" << liveAfter.size() << " socket=" << socket;

      if (fc > 0) {
        lsError() << "state-seq iter" << i << " => FOREIGN delivery observed; address reuse of a previous socket likely — late connectionStateChanged from old socket matched by address";
      } else if (uc == 0) {
        lsError() << "state-seq iter" << i << " => self Unconnected never delivered (queued delivery lost, or filtered out before counting)";
      } else {
        lsError() << "state-seq iter" << i << " => self Unconnected delivered but state is not Unconnected; a late connect/active path resurrected the socket";
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

  // 200 ms is ample for a refused connect on loopback; anything else is a bug.
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
    ++clientRecv;
    clientRecvId = id;
  });
  guards += server->received.connect([&](const DataArraySocket *s, const DataArray *d, uint32_t id) {
    {
      std::lock_guard<std::mutex> l(serverMutex);
      serverPayload.assign(d->begin(), d->end());
    }
    serverRecvId = id;
    ++serverRecv;
    const_cast<DataArraySocket *>(s)->transmit(*d, id + 100);  // echo back
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

      const bool gotClientRecv = wait_for([&] { return clientRecv.load() >= 1; }, 2000);
      EXPECT_MSG(gotClientRecv, "client did not receive echo within 2s");
      EXPECT_MSG(clientRecvId.load() == 142u, "client id=" + std::to_string(clientRecvId.load()) + " (expected 142)");
    }
  }

  guards.clear();
  client.destroySocket(cs);
  g_ledger.onDestroy(cs, 600);
  serverOwner->invoke([&]() { server->close(); }, true);
  // Workers self-reap when their last socket closes; give them a moment.
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

    test_da_connect_is_async(*dac);
    test_da_reconnect_after_failed_connect(*dac);
    test_da_rapid_accept_close(*dac);
    test_da_partial_frame_close(*dac);
    test_da_pool_teardown_on_last_socket(*dac);
    test_da_pool_reuse_after_teardown(*dac);
    test_da_state_sequence(*dac);
    test_da_no_reconnect_with_zero_timeout(*dac);
    test_tcp_server_roundtrip(&poolOwner, *dac);
    test_da_quit_from_foreign_thread(*dac);

    poolOwner.invoke([&dac]() {
      delete dac;
      dac = nullptr;
    }, true);
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
