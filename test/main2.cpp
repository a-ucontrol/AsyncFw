// Tests-main2: standalone DataArrayTcpClient suite.
//
// All test code runs on a dedicated AsyncFw::Thread (`testMain`) so that
// AbstractThread::current() is valid inside every FunctionConnector
// connection. Polling helpers read socket->state() directly.
//
// Contract pinned:
//   - connectToHost() is async and never blocks the caller.
//   - Failed connects settle to Unconnected; socket stays reconnectable.
//   - accept-close and partial-frame-close settle to Unconnected.
//   - Destroying the last socket reaps the worker thread.
//   - reconnectTimeout == 0 disables auto-reconnect.
//   - quit() from any thread synchronously tears the pool down on owner.

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
#include <AsyncFw/LogStream>
#include <AsyncFw/DataArrayTcpClient>
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

int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool waitForUnconnected(DataArraySocket *socket, int timeoutMs) {
  for (int i = 0; i < timeoutMs; ++i) {
    if (socket->state() == AbstractSocket::State::Unconnected) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return socket->state() == AbstractSocket::State::Unconnected;
}

AbstractSocket::State waitForTerminal(DataArraySocket *socket, int timeoutMs) {
  for (int i = 0; i < timeoutMs; ++i) {
    auto s = socket->state();
    if (s == AbstractSocket::State::Active || s == AbstractSocket::State::Unconnected) return s;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return socket->state();
}

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
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345);
  int64_t elapsed = now_ms() - t0;
  EXPECT_MSG(elapsed <= 50, "connectToHost blocked for " + std::to_string(elapsed) + "ms");
  client.destroySocket(socket);
}

// ============================================================================
// Failed connect settles to Unconnected; socket stays reconnectable.
// ============================================================================
void test_da_reconnect_after_failed_connect(DataArrayTcpClient &client) {
  lsNotice() << "=== reconnect after failed connect ===";
  DataArraySocket *socket = client.createSocket();

  client.connectToHost(socket, "192.0.2.1", 12345);
  if (!waitForUnconnected(socket, 2000)) {
    EXPECT_MSG(false, "first connect (unroutable) did not settle to Unconnected; state=" + std::to_string(static_cast<int>(socket->state())));
    client.destroySocket(socket);
    return;
  }

  client.connectToHost(socket, "127.0.0.1", 1);
  if (!waitForUnconnected(socket, 2000)) {
    EXPECT_MSG(false, "second connect (refused) did not settle to Unconnected; state=" + std::to_string(static_cast<int>(socket->state())));
    client.destroySocket(socket);
    return;
  }

  EXPECT(true);
  client.destroySocket(socket);
}

// ============================================================================
// accept-close stress: each iteration settles to Unconnected within 500 ms.
// ============================================================================
void test_da_rapid_accept_close(DataArrayTcpClient &client) {
  lsNotice() << "=== rapid accept-close stress ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  int stuck = 0;
  constexpr int kIters = 100;
  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    client.connectToHost(socket, "127.0.0.1", server.port);
    if (!waitForUnconnected(socket, 500)) {
      ++stuck;
      lsError() << "iter" << i << "did not settle to Unconnected in 500ms, state=" << static_cast<int>(socket->state());
    }
    client.destroySocket(socket);
  }

  EXPECT_MSG(stuck == 0, "stuck=" + std::to_string(stuck) + " of " + std::to_string(kIters));
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
  std::atomic<bool> gotCompleteFrame {false};
  FunctionConnectionGuard g = socket->received.connect([&gotCompleteFrame](const DataArray *, uint32_t) { gotCompleteFrame = true; });

  client.connectToHost(socket, "127.0.0.1", server.port);
  const bool settled = waitForUnconnected(socket, 2000);

  EXPECT_MSG(settled, "socket did not reach Unconnected within 2000ms; state=" + std::to_string(static_cast<int>(socket->state())));
  EXPECT_MSG(!gotCompleteFrame.load(), "complete frame delivered — scenario void");

  g = {};
  client.destroySocket(socket);
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
  client.connectToHost(socket, "127.0.0.1", server.port);
  waitForTerminal(socket, 1000);
  client.destroySocket(socket);
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
  client.connectToHost(s1, "127.0.0.1", server.port);
  waitForTerminal(s1, 1000);
  client.destroySocket(s1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  DataArraySocket *s2 = client.createSocket();
  EXPECT_MSG(s2 != nullptr, "createSocket returned nullptr after teardown");
  if (!s2) return;
  client.connectToHost(s2, "127.0.0.1", server.port);
  auto st = waitForTerminal(s2, 1000);
  EXPECT_MSG(st == AbstractSocket::State::Active || st == AbstractSocket::State::Unconnected, "fresh socket in unexpected state " + std::to_string(static_cast<int>(st)));
  client.destroySocket(s2);
}

// ============================================================================
// State transition order on accept-close: Active precedes Unconnected.
// ============================================================================
void test_da_state_sequence(DataArrayTcpClient &client) {
  lsNotice() << "=== state sequence on accept-close ===";
  RawServer server;
  if (!server.start(1)) {
    EXPECT_MSG(false, "server start failed");
    return;
  }

  int badSequences = 0;
  constexpr int kIters = 20;
  for (int i = 0; i < kIters; ++i) {
    DataArraySocket *socket = client.createSocket();
    std::mutex m;
    std::vector<AbstractSocket::State> seq;
    std::atomic<bool> settled {false};

    FunctionConnectionGuard g = socket->stateChanged.connect([&m, &seq, &settled](AbstractSocket::State s) {
      std::lock_guard<std::mutex> l(m);
      seq.push_back(s);
      if (s == AbstractSocket::State::Unconnected) settled = true;
    });

    client.connectToHost(socket, "127.0.0.1", server.port);

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
      for (auto s : seq) dump += std::to_string(static_cast<int>(s)) + " ";
      lsError() << "iter" << i << "bad sequence:[" << dump << "]";
    }

    client.destroySocket(socket);
  }

  EXPECT_MSG(badSequences == 0, std::to_string(badSequences) + " of " + std::to_string(kIters) + " had a bad state sequence");
}

// ============================================================================
// reconnectTimeout == 0 disables auto-reconnect.
// ============================================================================
void test_da_no_reconnect_with_zero_timeout(DataArrayTcpClient &client) {
  lsNotice() << "=== no auto-reconnect (reconnectTimeout == 0) ===";
  DataArraySocket *socket = client.createSocket();

  std::atomic<int> transitionCount {0};
  FunctionConnectionGuard g = socket->stateChanged.connect([&transitionCount](AbstractSocket::State) { ++transitionCount; });

  client.connectToHost(socket, "127.0.0.1", 1);
  const bool settled = waitForUnconnected(socket, 500);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const int n = transitionCount.load();
  g = {};

  EXPECT_MSG(settled, "socket did not reach Unconnected within 500ms; state=" + std::to_string(static_cast<int>(socket->state())));
  EXPECT_MSG(n <= 3, "too many state transitions (" + std::to_string(n) + ") — reconnect likely active");
  client.destroySocket(socket);
}

// ============================================================================
// quit() from a foreign thread synchronously tears the pool down.
//
// AbstractThreadPool::quit() dispatches teardown to the pool owner via
// invoke(..., true), so calling it from any thread is safe: it blocks
// until the owner has destroyed all workers and their sockets.
// ============================================================================
void test_da_quit_from_foreign_thread(DataArrayTcpClient &client) {
  lsNotice() << "=== quit() from foreign thread ===";

  DataArraySocket *socket = client.createSocket();
  EXPECT_MSG(socket != nullptr, "createSocket failed");
  EXPECT_MSG(client.threads()->size() > 0, "pool has no workers before quit");

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
