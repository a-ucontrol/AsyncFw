/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

/*
 * Misc example: DataArrayTcpClient::connectToHost blocking-path reproducer.
 *
 * Scenarios:
 *   Test 1  caller-side timeout on unroutable IP       — state-leak check
 *   Test 2  second connect after a timeout             — state-machine check
 *   Test 3  fire-and-forget (timeout = 0)              — non-blocking check
 *   Test 4  rapid accept-close stress (N iterations)   — UAF under ASAN/TSAN
 *   Test 5  partial frame then close                   — buffer-cleanup check
 *
 * Run Test 4 under ASAN (or TSan) to catch the QueuedTask UAF on `waiter`:
 *   - socket emits stateChanged(Active)     → wait() returns
 *   - socket emits stateChanged(Unconnected) microseconds later
 *   - the second Queued task references the already-destroyed stack Waiter
 */

#include <AsyncFw/MainThread>
#include <AsyncFw/DataArrayTcpClient>
#include <AsyncFw/DataArraySocket>
#include <AsyncFw/Thread>
#include <AsyncFw/LogStream>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#ifndef _WIN32
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <unistd.h>
#endif

using namespace AsyncFw;

namespace {

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void dump_socket(const char *tag, const DataArraySocket *s) {
  lsNotice() << tag
             << "state:" << static_cast<int>(s->state());
}

// ---- Test 1: caller-side timeout on unroutable IP ---------------------
void test_timeout_unroutable(DataArrayTcpClient &client) {
  lsNotice() << "=== Test 1: caller-side timeout on unroutable IP ===";
  DataArraySocket *socket = client.createSocket();
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345, 500);
  int64_t elapsed = now_ms() - t0;
  auto st = socket->state();

  if (elapsed >= 400 && elapsed <= 1500) {
    if (st == AbstractSocket::Unconnected)
      lsNotice() << "PASS: caller-side timeout fired in" << elapsed << "ms, socket Unconnected";
    else
      lsError() << "FAIL (state leak): caller-side timeout fired in" << elapsed << "ms,"
                << "socket stuck in state" << static_cast<int>(st)
                << "(expected 0 / Unconnected). Caller-side timer must disconnect() the socket.";
  } else if (elapsed < 400) {
    lsWarning() << "INCONCLUSIVE: connect failed fast in" << elapsed
                << "ms (ICMP unreachable?), socket state =" << static_cast<int>(st);
  } else {
    lsWarning() << "INCONCLUSIVE: timeout took too long:" << elapsed << "ms";
  }
  dump_socket("after-timeout", socket);
  client.destroySocket(socket);
}

// ---- Test 2: second connect after a timeout ---------------------------
void test_timeout_then_reconnect(DataArrayTcpClient &client) {
  lsNotice() << "=== Test 2: second connect after timeout ===";
  DataArraySocket *socket = client.createSocket();

  client.connectToHost(socket, "192.0.2.1", 12345, 300);
  dump_socket("after-first", socket);

  if (socket->state() != AbstractSocket::Unconnected)
    lsError() << "FAIL: first connect left socket in state"
              << static_cast<int>(socket->state()) << "- second connect will fail";

  client.connectToHost(socket, "127.0.0.1", 1, 300);
  dump_socket("after-second", socket);
  client.destroySocket(socket);
}

// ---- Test 3: fire-and-forget (timeout = 0) ----------------------------
void test_fire_and_forget(DataArrayTcpClient &client) {
  lsNotice() << "=== Test 3: fire-and-forget (timeout = 0) ===";
  DataArraySocket *socket = client.createSocket();
  int64_t t0 = now_ms();
  client.connectToHost(socket, "192.0.2.1", 12345, 0);
  int64_t elapsed = now_ms() - t0;

  if (elapsed > 50)
    lsError() << "FAIL: fire-and-forget blocked for" << elapsed << "ms";
  else
    lsNotice() << "PASS: fire-and-forget returned in" << elapsed << "ms";
  dump_socket("after-fire-and-forget", socket);
  client.destroySocket(socket);
}

#ifndef _WIN32
// Raw POSIX listener driving specific client-side transitions.
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
            ::close(fd);  // immediate close: Active then Unconnected
          } else if (mode == 2) {
            // partial DataArraySocket frame header then close
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

// ---- Test 4: rapid accept-close stress --------------------------------
void test_rapid_accept_close(DataArrayTcpClient &client, int iterations) {
  lsNotice() << "=== Test 4: rapid accept-close stress (" << iterations << " iters) ===";
  lsNotice() << "Run under ASAN/TSAN to catch the QueuedTask UAF on Waiter";

  RawServer server;
  if (!server.start(1)) { lsError() << "server start failed"; return; }

  int stuck = 0;
  for (int i = 0; i < iterations; ++i) {
    DataArraySocket *socket = client.createSocket();
    client.connectToHost(socket, "127.0.0.1", server.port, 2000);
    auto st = socket->state();
    if (st != AbstractSocket::Unconnected) {
      ++stuck;
      lsWarning() << "iter" << i << "state after connect ="
                  << static_cast<int>(st);
    }
    // Give the pending Queued(Unconnected) task a chance to run
    // against the already-destroyed stack Waiter.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    client.destroySocket(socket);
  }
  if (stuck == 0)
    lsNotice() << "PASS:" << iterations << "iterations, all sockets Unconnected";
  else
    lsError() << "FAIL:" << stuck << "of" << iterations
              << "sockets stuck in non-Unconnected state";
}

// ---- Test 5: partial frame then close ---------------------------------
void test_partial_receive(DataArrayTcpClient &client) {
  lsNotice() << "=== Test 5: partial frame then close ===";
  RawServer server;
  if (!server.start(2)) { lsError() << "server start failed"; return; }

  DataArraySocket *socket = client.createSocket();
  client.connectToHost(socket, "127.0.0.1", server.port, 2000);
  dump_socket("after-partial", socket);
  if (socket->state() == AbstractSocket::Unconnected)
    lsNotice() << "PASS: socket Unconnected after partial-frame close";
  else
    lsError() << "FAIL: socket state =" << static_cast<int>(socket->state());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.destroySocket(socket);
}
#endif  // !_WIN32

}  // namespace

int main(int, char **) {
  LogStream::setTimeFormat("%H:%M:%S", true);

  DataArrayTcpClient client;
  client.setConnectTimeout(2000);
  client.setReconnectTimeout(0);

  client.connectionStateChanged.connect([](const DataArraySocket *s) {
    lsInfo() << "connectionStateChanged state:" << static_cast<int>(s->state());
  });

  Thread::current()->started.connect([&client]() {
    test_timeout_unroutable(client);
    test_timeout_then_reconnect(client);
    test_fire_and_forget(client);
#ifndef _WIN32
    test_rapid_accept_close(client, 100);
    test_partial_receive(client);
#endif
    lsNotice() << "=== All scenarios done ===";
    client.quit();
    Thread::current()->invoke([]() { MainThread::exit(); });
  });

  lsNotice() << "Start Application";
  int ret = MainThread::exec();
  lsNotice() << "End Application" << ret;
  return ret;
}
