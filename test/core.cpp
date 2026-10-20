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

#ifndef _WIN32
  #include <poll.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <dirent.h>
#endif

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

#define EXPECT_MSG(cond, msg)                                                                               \
  do {                                                                                                      \
    ++g_total;                                                                                              \
    if (!(cond)) {                                                                                          \
      ++g_failed;                                                                                           \
      lsError() << LogStream::Color::Red << "FAIL" << __FILE__ << ':' << __LINE__ << #cond << "—" << (msg); \
    }                                                                                                       \
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
std::atomic<int> Tracker::copies {0};
std::atomic<int> Tracker::moves {0};

// ---------------------------------------------------------------------------
// TST — probe object for copy/move semantics verification.
// Usage from examples: logging ctors is enough to eyeball behaviour; here
// it is instrumented with counters so tests can assert exact invariants.
//
// Invariants enforced by tests:
//   - A subscriber receiving a value-parameter sees a valid val (!= -1).
//   - The sender's original object is not moved-from after lvalue emit.
//   - The sender's original object IS moved-from after xvalue emit.
//   - A TST captured into a lambda is not moved-from by emit.
//   - Direct/Sync emit does not move: moves_ unchanged.
//   - Queued emit moves from the per-task tuple into each subscriber
//     (the tuple is consumed by ~QueuedTask): moves_ == N (or N+1 for xvalue
//     emit, where the dispatcher's value-param is also move-initialized).
//   - Live-instance counter returns to baseline at test scope end.
// ---------------------------------------------------------------------------
struct TST {
  static std::atomic<int> copies_;
  static std::atomic<int> moves_;
  static std::atomic<int> live_;

  int val = 0;

  explicit TST(int v = 0) : val(v) {
    ++live_;
    lsDebug() << "ctor" << val;
  }
  TST(const TST &o) : val(o.val) {
    ++live_;
    ++copies_;
    lsDebug() << "copy" << val;
  }
  TST(TST &&o) noexcept : val(o.val) {
    o.val = -1;  // moved-from marker
    ++live_;
    ++moves_;
    lsDebug() << "move" << val;
  }
  TST &operator=(const TST &o) {
    val = o.val;
    ++copies_;
    lsDebug() << "copy=" << val;
    return *this;
  }
  TST &operator=(TST &&o) noexcept {
    if (this != &o) {
      val = o.val;
      o.val = -1;
      ++moves_;
      lsDebug() << "move=" << val;
    }
    return *this;
  }
  ~TST() {
    --live_;
    lsDebug() << "dtor" << val;
  }
};
// Free function keeps clang-analyzer's moved-from tracking out of tests that
// intentionally inspect a moved-from object (well-defined for TST: only int).
inline bool isMovedFrom(const TST &t) { return t.val == -1; }

std::atomic<int> TST::copies_ {0};
std::atomic<int> TST::moves_ {0};
std::atomic<int> TST::live_ {0};

// Convenience: baseline/restore copies/moves deltas per test.
struct TSTMeter {
  int copies;
  int moves;
  int live;
  TSTMeter() : copies(TST::copies_.load()), moves(TST::moves_.load()), live(TST::live_.load()) {}
  int copiesDelta() const { return TST::copies_.load() - copies; }
  int movesDelta() const { return TST::moves_.load() - moves; }
  int liveDelta() const { return TST::live_.load() - live; }
};

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

  Tracker original {7};
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

// ---------------------------------------------------------------------------
// TST — Direct path semantics
// ---------------------------------------------------------------------------

// Single Direct subscriber with a value-parameter receives a copy.
// Emit must not move from the sender's original.
void test_tst_direct_single_subscriber_no_move() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    int seen = -1;
    fc.connect<Conn::Direct>([&](TST t) { seen = t.val; });

    TST original {7};
    int moves_before_emit = TST::moves_.load();
    fc(original);

    EXPECT(seen == 7);
    EXPECT(!isMovedFrom(original));
    EXPECT(original.val == 7);
    // Direct emit must not move.
    EXPECT(TST::moves_.load() == moves_before_emit);
  }
  EXPECT(meter.liveDelta() == 0);
}

// Three Direct subscribers, each sees a valid copy of the original value.
// Regression target: move-steal by the first subscriber (see invocable.hpp).
void test_tst_direct_multicast_no_move() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    int v1 = -1, v2 = -1, v3 = -1;
    fc.connect<Conn::Direct>([&](TST t) { v1 = t.val; });
    fc.connect<Conn::Direct>([&](TST t) { v2 = t.val; });
    fc.connect<Conn::Direct>([&](TST t) { v3 = t.val; });

    TST original {42};
    int copies_before_emit = TST::copies_.load();
    int moves_before_emit = TST::moves_.load();
    fc(original);

    EXPECT(v1 == 42);
    EXPECT(v2 == 42);
    EXPECT(v3 == 42);
    EXPECT(!isMovedFrom(original));
    EXPECT(original.val == 42);
    EXPECT(TST::copies_.load() - copies_before_emit >= 3);
    EXPECT(TST::moves_.load() == moves_before_emit);
  }
  EXPECT(meter.liveDelta() == 0);
}

// Captured TST inside a lambda must survive emit unchanged.
// Covers both lvalue and rvalue capture at connect() time.
void test_tst_direct_lambda_capture_not_mutated() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;

    TST cap_lv {111};
    auto lv_lambda = [cap_lv](TST t) { EXPECT_MSG(t.val == 42, "lv subscriber got t.val"); };
    fc.connect<Conn::Direct>(lv_lambda);

    TST cap_rv {222};
    fc.connect<Conn::Direct>([cap_rv](TST t) { EXPECT_MSG(t.val == 42, "rv subscriber got t.val"); });

    TST original {42};
    fc(original);
    fc(original);  // emit twice: neither capture must be moved-from

    EXPECT(!isMovedFrom(cap_lv));
    EXPECT(cap_lv.val == 111);
    EXPECT(!isMovedFrom(cap_rv));
    EXPECT(cap_rv.val == 222);
    EXPECT(!isMovedFrom(original));
  }
  EXPECT(meter.liveDelta() == 0);
}

// Lvalue emit: FunctionConnector::operator()(TST args) takes params by value,
// so emit implies 1 copy into `args`, then N copies for N value-param
// subscribers. Zero moves — invariants from invocable.hpp hold.
void test_tst_direct_exact_copy_count_lvalue() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    int hits = 0;
    fc.connect<Conn::Direct>([&](TST) { ++hits; });
    fc.connect<Conn::Direct>([&](TST) { ++hits; });
    fc.connect<Conn::Direct>([&](TST) { ++hits; });

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(original);
    EXPECT(hits == 3);
    EXPECT(TST::copies_.load() - c0 == 4);  // 1 (param) + 3 (subscribers)
    EXPECT(TST::moves_.load() - m0 == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

// Prvalue emit: C++17 guaranteed copy elision constructs TST{7} directly into
// `args`. Zero moves, N copies (one per value-param subscriber).
void test_tst_direct_exact_copy_count_prvalue() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    int hits = 0;
    fc.connect<Conn::Direct>([&](TST) { ++hits; });

    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(TST {7});  // prvalue → guaranteed elision into param, no move
    EXPECT(hits == 1);
    EXPECT(TST::copies_.load() - c0 == 1);  // 1 subscriber
    EXPECT(TST::moves_.load() - m0 == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

// Xvalue emit: std::move(original) moves into `args` (1 move), then each
// subscriber still gets a valid copy. The sender's object becomes moved-from
// — this is the caller's responsibility, not a move-steal by the framework.
void test_tst_direct_exact_count_xvalue() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    int hits = 0;
    fc.connect<Conn::Direct>([&](TST) { ++hits; });

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(std::move(original));  // xvalue → move-init into param
    EXPECT(hits == 1);
    EXPECT(TST::copies_.load() - c0 == 1);  // subscriber
    EXPECT(TST::moves_.load() - m0 == 1);   // param init from xvalue
    EXPECT(isMovedFrom(original));          // sender's object moved-from
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Queued path semantics
// ---------------------------------------------------------------------------
//
// QueuedTask stores a per-task tuple (args_ = std::tuple<decay_t<Args>...>).
// The tuple lives exactly until ~QueuedTask, so moving from it into the
// subscriber's value-param is safe and intended — unlike Direct/Sync, where
// the dispatcher's parameter is shared across the emit loop and must be
// copied.
//
// std::apply(*f_, args_) calls AbstractFunction::operator()(Args&...)
// (lvalue-ref), then Function::operator() forwards Args to the subscriber:
//   operator()(Args&... args) { f_(std::forward<Args>(args)...); }
// std::forward<TST>(lvalue) yields an xvalue → move into the value-param
// subscriber. So the tuple element is moved-from, but it is per-task and
// about to be destroyed anyway.
//
// Expected deltas:
//   lvalue emit: 1 copy (param) + N copies (tuple) + N moves (apply→subscriber).
//   xvalue emit: 1 move (param) + N copies (tuple) + N moves (apply→subscriber).
// Zero move-steal: each subscriber receives its own per-task tuple element.

void test_tst_queued_cross_thread_per_task_move() {
  TSTMeter meter;
  {
    Thread target("tst-queued");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect<Conn::Queued>([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect<Conn::Queued>([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(original);

    EXPECT(wait_for([&] { return hits.load() == 2; }));
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(!isMovedFrom(original));
    EXPECT(original.val == 7);
    // lvalue emit: 1 param copy + 2 tuple copies + 2 subscriber moves.
    EXPECT_MSG(TST::copies_.load() - c0 == 3, "copies delta=" + std::to_string(TST::copies_.load() - c0) + " (expected 3: 1 param + 2 tuple)");
    EXPECT_MSG(TST::moves_.load() - m0 == 2, "moves delta=" + std::to_string(TST::moves_.load() - m0) + " (expected 2: per-task tuple→subscriber moves)");
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_queued_cross_thread_xvalue() {
  TSTMeter meter;
  {
    Thread target("tst-queued-xv");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect<Conn::Queued>([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect<Conn::Queued>([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(std::move(original));

    EXPECT(wait_for([&] { return hits.load() == 2; }));
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(isMovedFrom(original));
    // xvalue emit: 1 param move + 2 tuple copies + 2 subscriber moves.
    EXPECT_MSG(TST::copies_.load() - c0 == 2, "copies delta=" + std::to_string(TST::copies_.load() - c0) + " (expected 2: tuple only)");
    EXPECT_MSG(TST::moves_.load() - m0 == 3, "moves delta=" + std::to_string(TST::moves_.load() - m0) + " (expected 3: 1 param + 2 per-task)");
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Sync path semantics
// ---------------------------------------------------------------------------

void test_tst_sync_cross_thread_no_move() {
  TSTMeter meter;
  {
    Thread target("tst-sync");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect<Conn::Sync>([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect<Conn::Sync>([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(original);

    EXPECT(hits.load() == 2);
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(!isMovedFrom(original));
    EXPECT(original.val == 7);
    // Sync: invoke(Args&...) copy, no move.
    EXPECT(TST::moves_.load() - m0 == 0);
    EXPECT(TST::copies_.load() - c0 >= 2);
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_sync_cross_thread_xvalue() {
  TSTMeter meter;
  {
    Thread target("tst-sync-xv");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect<Conn::Sync>([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect<Conn::Sync>([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    int m0 = TST::moves_.load();
    fc(std::move(original));

    EXPECT(hits.load() == 2);
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(isMovedFrom(original));
    // Only the param init from xvalue; invoke() is copy.
    EXPECT(TST::moves_.load() - m0 == 1);
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Policies (DirectOnly / QueuedOnly / SyncOnly)
// ---------------------------------------------------------------------------

void test_tst_policy_direct_only_no_move() {
  TSTMeter meter;
  {
    FunctionConnector<TST>::Policy<AbstractFunctionConnector::DirectOnly> fc;
    int v1 = -1, v2 = -1;
    fc.connect([&](TST t) { v1 = t.val; });
    fc.connect([&](TST t) { v2 = t.val; });

    TST original {7};
    int c0 = TST::copies_.load();
    int m0 = TST::moves_.load();
    fc(original);

    EXPECT(v1 == 7);
    EXPECT(v2 == 7);
    EXPECT(!isMovedFrom(original));
    EXPECT(TST::copies_.load() - c0 >= 2);
    EXPECT(TST::moves_.load() - m0 == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

// QueuedOnly: per-task tuple move applies; do not pin exact counts.
void test_tst_policy_queued_only() {
  TSTMeter meter;
  {
    Thread target("tst-qonly");
    target.start();

    FunctionConnector<TST>::Policy<AbstractFunctionConnector::QueuedOnly> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    fc(original);

    EXPECT(wait_for([&] { return hits.load() == 2; }));
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(!isMovedFrom(original));
    // No invariant on moves: Queued uses per-task tuple move by design.
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_policy_sync_only_no_move() {
  TSTMeter meter;
  {
    Thread target("tst-sonly");
    target.start();

    FunctionConnector<TST>::Policy<AbstractFunctionConnector::SyncOnly> fc;
    std::atomic<int> v1 {-1}, v2 {-1};
    std::atomic<int> hits {0};

    target.invoke([&]() {
      fc.connect([&](TST t) {
        v1 = t.val;
        ++hits;
      });
      fc.connect([&](TST t) {
        v2 = t.val;
        ++hits;
      });
    }, true);

    TST original {7};
    int m0 = TST::moves_.load();
    fc(original);

    EXPECT(hits.load() == 2);
    EXPECT(v1.load() == 7);
    EXPECT(v2.load() == 7);
    EXPECT(!isMovedFrom(original));
    EXPECT(TST::moves_.load() - m0 == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Emit from plain std::thread
// ---------------------------------------------------------------------------
// FunctionConnector::operator() never calls AbstractThread::current() on the
// emit path, regardless of connection policy.

void test_tst_emit_from_std_thread_direct() {
  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    std::atomic<int> hits {0};
    std::atomic<int> invalidHits {0};
    fc.connect<Conn::Direct>([&](TST t) {
      if (t.val != 7) ++invalidHits;
      ++hits;
    });

    std::thread emitter([&]() {
      TST local {7};
      fc(local);
    });
    emitter.join();

    EXPECT(hits.load() == 1);
    EXPECT(invalidHits.load() == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_emit_from_std_thread_queued() {
  TSTMeter meter;
  {
    Thread target("tst-std-emit-q");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> hits {0};
    std::atomic<int> invalidHits {0};
    std::atomic<std::thread::id> observed {};

    target.invoke([&]() {
      fc.connect<Conn::Queued>([&](TST t) {
        if (t.val != 7) ++invalidHits;
        observed = std::this_thread::get_id();
        ++hits;
      });
    }, true);

    std::thread emitter([&]() {
      TST local {7};
      fc(local);
    });
    emitter.join();

    EXPECT(wait_for([&] { return hits.load() == 1; }));
    EXPECT(invalidHits.load() == 0);
    EXPECT(observed.load() == target.id());
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_emit_from_std_thread_sync() {
  TSTMeter meter;
  {
    Thread target("tst-std-emit-s");
    target.start();

    FunctionConnector<TST> fc;
    std::atomic<int> hits {0};
    std::atomic<int> invalidHits {0};

    target.invoke([&]() {
      fc.connect<Conn::Sync>([&](TST t) {
        if (t.val != 7) ++invalidHits;
        ++hits;
      });
    }, true);

    std::thread emitter([&]() {
      TST local {7};
      fc(local);
    });
    emitter.join();

    EXPECT(hits.load() == 1);
    EXPECT(invalidHits.load() == 0);
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Lambda capture read inside subscriber (Queued)
// ---------------------------------------------------------------------------
// Mirror of example/FunctionConnector/main.cpp: TST captured into a lambda
// connected Queued, then read inside the subscriber on every emit.
//
// Emit counters reflect per-task tuple move (see Group D header):
//   lvalue emit: 1 param copy + 2 tuple copies + 2 subscriber moves
//                = c_emit=3, m_emit=2.
// Counters must be read AFTER both queued tasks complete (wait_for) because
// the subscriber-side moves run on the target thread.
void test_tst_queued_lambda_capture_read_inside_subscriber() {
  TSTMeter meter;
  {
    Thread target("tst-cap-queued");
    target.start();

    FunctionConnector<int, TST> fc;
    std::atomic<int> hits {0};
    std::atomic<int> captureOk {0};
    std::atomic<int> captureBad {0};

    int c_before_connect = TST::copies_.load();
    int m_before_connect = TST::moves_.load();

    TST captured_lv {999};
    auto lambda = [&, captured_lv](int, TST tst) {
      if (captured_lv.val == 999 && tst.val == 42) ++captureOk;
      else ++captureBad;
      ++hits;
    };

    TST captured_rv {777};
    target.invoke([&]() {
      fc.connect<Conn::Queued>(lambda);
      fc.connect<Conn::Queued>([&, captured_rv](int, TST tst) {
        if (captured_rv.val == 777 && tst.val == 42) ++captureOk;
        else ++captureBad;
        ++hits;
      });
    }, true);

    int c_connect = TST::copies_.load() - c_before_connect;
    int m_connect = TST::moves_.load() - m_before_connect;
    EXPECT_MSG(c_connect == 3, "c_connect=" + std::to_string(c_connect) + " (expected 3: 2 for lv lambda, 1 for rv capture)");
    EXPECT_MSG(m_connect == 1, "m_connect=" + std::to_string(m_connect) + " (expected 1: rv lambda moved into Function)");

    EXPECT(!isMovedFrom(captured_lv));
    EXPECT(!isMovedFrom(captured_rv));
    EXPECT(captured_lv.val == 999);
    EXPECT(captured_rv.val == 777);

    TST arg {42};
    int c_before_emit = TST::copies_.load();
    int m_before_emit = TST::moves_.load();
    fc(0, arg);

    EXPECT(wait_for([&] { return hits.load() == 2; }));

    int c_emit = TST::copies_.load() - c_before_emit;
    int m_emit = TST::moves_.load() - m_before_emit;
    // lvalue emit with 2 Queued subscribers:
    //   1 param copy + 2 tuple copies + 2 apply→subscriber moves.
    EXPECT_MSG(c_emit == 3, "c_emit=" + std::to_string(c_emit) + " m_emit=" + std::to_string(m_emit) + " (expected 3 copies / 2 moves)");
    EXPECT_MSG(m_emit == 2, "m_emit=" + std::to_string(m_emit) + " (expected 2: per-task)");

    EXPECT(captureBad.load() == 0);
    EXPECT(captureOk.load() == 2);

    EXPECT(!isMovedFrom(captured_lv));
    EXPECT(!isMovedFrom(captured_rv));
    EXPECT(captured_lv.val == 999);
    EXPECT(captured_rv.val == 777);
    EXPECT(!isMovedFrom(arg));
  }
  EXPECT(meter.liveDelta() == 0);
}

// ---------------------------------------------------------------------------
// TST — Concurrency stress
// ---------------------------------------------------------------------------

void test_tst_concurrent_emit_direct_stress() {
  constexpr int kThreads = 8;
  constexpr int kIterations = 200;
  constexpr int kSubscribers = 3;

  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    std::atomic<int> hits {0};
    std::atomic<int> invalidHits {0};
    std::atomic<int> movedFromHits {0};

    for (int s = 0; s < kSubscribers; ++s) {
      fc.connect<Conn::Direct>([&](TST t) {
        if (t.val < 0) ++movedFromHits;
        else if (t.val >= kThreads) ++invalidHits;
        hits.fetch_add(1, std::memory_order_relaxed);
      });
    }

    int m0 = TST::moves_.load();

    std::vector<std::thread> emitters;
    emitters.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
      emitters.emplace_back([&fc, i]() {
        for (int k = 0; k < kIterations; ++k) {
          TST local {i};
          fc(local);
        }
      });
    }
    for (auto &t : emitters) t.join();

    EXPECT(hits.load() == kThreads * kIterations * kSubscribers);
    EXPECT(movedFromHits.load() == 0);
    EXPECT(invalidHits.load() == 0);
    EXPECT(TST::moves_.load() == m0);
  }
  EXPECT(meter.liveDelta() == 0);
}

void test_tst_concurrent_emit_and_connect_stress() {
  constexpr int kEmitters = 4;
  constexpr int kIterations = 100;

  TSTMeter meter;
  {
    FunctionConnector<TST> fc;
    std::atomic<int> movedFromHits {0};
    std::atomic<int> invalidHits {0};
    std::atomic<bool> stop {false};

    for (int s = 0; s < 2; ++s) {
      fc.connect<Conn::Direct>([&](TST t) {
        if (t.val < 0) ++movedFromHits;
        else if (t.val >= kEmitters) ++invalidHits;
      });
    }

    std::vector<std::thread> emitters;
    emitters.reserve(kEmitters);
    for (int i = 0; i < kEmitters; ++i) {
      emitters.emplace_back([&fc, i]() {
        for (int k = 0; k < kIterations; ++k) {
          TST local {i};
          fc(local);
        }
      });
    }

    Thread churn("tst-churn");
    churn.start();
    churn.invoke([&]() {
      while (!stop.load()) {
        FunctionConnectionGuard g = fc.connect<Conn::Direct>([&](TST t) {
          if (t.val < 0) ++movedFromHits;
          else if (t.val >= kEmitters) ++invalidHits;
        });
        std::this_thread::yield();
      }
    });

    for (auto &t : emitters) t.join();
    stop = true;
    churn.invoke([]() {}, true);

    EXPECT(movedFromHits.load() == 0);
    EXPECT(invalidHits.load() == 0);
  }
  EXPECT(meter.liveDelta() == 0);
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
  std::atomic<int> got {0};
  std::atomic<bool> done {false};

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
  std::atomic<int> got {0};

  target.invoke([&]() { fc.connect<Conn::Sync>([&](int v) { got = v; }); }, true);

  fc(123);
  EXPECT(got == 123);
}

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
  std::atomic<int> received {0};
  std::atomic<bool> stop {false};

  FunctionConnectionGuard *guard = new FunctionConnectionGuard(fc.connect<Conn::Direct>([&](int v) {
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
    std::atomic<int> ready {0};
    std::atomic<bool> go {false};
    std::atomic<int> done {0};

    auto *fc = new FunctionConnector<int>;
    auto *g = new FunctionConnectionGuard(fc->connect<Conn::Direct>([](int) {}));

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

void test_thread_waits_for_running_task() {
  std::atomic<bool> taskFinished {false};
  {
    std::atomic<bool> taskStarted {false};
    Thread t("probe");
    t.start();
    t.invoke([&]() {
      taskStarted = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      taskFinished = true;
    });
    while (!taskStarted.load()) std::this_thread::yield();
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
  std::atomic<int> started_count {0};
  std::atomic<int> finished_count {0};
  FunctionConnectionGuardList guards;
  guards += FunctionConnectionGuard(Thread::current()->started.connect<Conn::Direct>([&]() { ++started_count; }));
  guards += FunctionConnectionGuard(Thread::current()->finished.connect<Conn::Direct>([&]() { ++finished_count; }));

  int fds_before = open_fd_count();

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

  EXPECT(fds_after_1 == fds_before + 1);
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
    lsDebug() << LogStream::Color::Cyan << "state=" << static_cast<int>(state_) << "error=" << static_cast<int>(error()) << "msg=" << errorString();
  }
};
#ifndef _WIN32
int rawListen(int &out_fd) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in sa {};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = 0;
  if (::bind(fd, (sockaddr *)&sa, sizeof(sa)) < 0) {
    ::close(fd);
    return -1;
  }
  if (::listen(fd, 1) < 0) {
    ::close(fd);
    return -1;
  }
  socklen_t len = sizeof(sa);
  if (::getsockname(fd, (sockaddr *)&sa, &len) < 0) {
    ::close(fd);
    return -1;
  }
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
  pollfd pfd {listen_fd, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0) return -1;
  sockaddr_in sa {};
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

    linger lg {1, 0};
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
#endif

// ============================================================================
// Group E. AbstractThread::Waiter
// ============================================================================

void test_waiter_complete_from_same_thread() {
  Thread t("waiter-basic");
  t.start();

  std::atomic<bool> timerFired {false};
  std::atomic<bool> waitReturned {false};

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

void test_waiter_nested() {
  Thread t("waiter-nested");
  t.start();

  std::atomic<int> done {0};
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

// complete() routed through the target thread's event loop is the supported
// cross-thread pattern: the caller does not touch Waiter state directly.
void test_waiter_complete_via_invoke() {
  Thread t("waiter-via-invoke");
  t.start();

  std::atomic<Thread::Waiter *> waiterPtr {nullptr};
  std::atomic<bool> waitReturned {false};

  t.invoke([&]() {
    Thread::Waiter w;
    waiterPtr = &w;
    w.wait();
    waiterPtr = nullptr;
    waitReturned = true;
  });

  EXPECT(wait_for([&] { return waiterPtr.load() != nullptr; }, 250));

  // Supported pattern: dispatch complete() into the target thread.
  t.invoke([&]() { waiterPtr.load()->complete(); });

  EXPECT(wait_for([&] { return waitReturned.load(); }, 500));
}

void test_waiter_quit_from_nested_exec_nested_count() {
  AsyncFw::Thread thread("WaiterQuitTest");

  bool _error = false;
  AsyncFw::Thread::Waiter _w1;
  AsyncFw::Thread::Waiter _w2;
  thread.started.connect([&_w1, &_w2, &thread, &_error]() {
    AsyncFw::Timer::single(1000, [&thread, &_error]() {
      _error = true;
      lsError() << "timeout";
      thread.quit();
    });
    AsyncFw::Timer::single(50, [&thread]() { thread.quit(); });
    AsyncFw::Timer::single(500, [&_w1, &_w2]() {
      _w1.complete();
      _w2.complete();
    });
    AsyncFw::Thread::current()->invoke([&_w1]() {
      lsInfo() << "start wait 1";
      _w1.wait();
      lsInfo() << "end wait 1";
    });
    AsyncFw::Thread::current()->invoke([&_w2]() {
      lsInfo() << "start wait 2";
      _w2.wait();
      lsInfo() << "end wait 2";
    });
  });

  lsNotice() << "start";
  thread.start();
  thread.waitFinished();
  lsNotice() << "end";

  EXPECT(!_error);
}

}  // namespace

// ============================================================================
// main
// ============================================================================

int main(int, char **) {
  lsNotice() << "set log output";
  LogStream::setCompleted(&log_output);

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

  // TST — Direct path
  test_tst_direct_single_subscriber_no_move();
  test_tst_direct_multicast_no_move();
  test_tst_direct_lambda_capture_not_mutated();
  test_tst_direct_exact_copy_count_lvalue();
  test_tst_direct_exact_copy_count_prvalue();
  test_tst_direct_exact_count_xvalue();

  // TST — Queued path
  test_tst_queued_cross_thread_per_task_move();
  test_tst_queued_cross_thread_xvalue();

  // TST — Sync path
  test_tst_sync_cross_thread_no_move();
  test_tst_sync_cross_thread_xvalue();

  // TST — Policies
  test_tst_policy_direct_only_no_move();
  test_tst_policy_queued_only();
  test_tst_policy_sync_only_no_move();

  // TST — Emit from plain std::thread
  test_tst_emit_from_std_thread_direct();
  test_tst_emit_from_std_thread_queued();
  test_tst_emit_from_std_thread_sync();

  // TST — Lambda capture under Queued
  test_tst_queued_lambda_capture_read_inside_subscriber();

  // TST — Concurrency stress
  test_tst_concurrent_emit_direct_stress();
  test_tst_concurrent_emit_and_connect_stress();

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
  test_mainthread_reexec();
#ifndef _WIN32
  // --- Group D: AbstractSocket HUP/ERR --------------------------------
  Thread::current()->started.connect([]() {
    test_connect_refused([]() {
      test_graceful_close([]() {
        test_rst_close([]() {
          lsNotice() << "=== AbstractSocket tests done ===";
          MainThread::exit();
        });
      });
    });
  });
#endif
  // --- Group E: AbstractThread::Waiter ---------------------------------
  test_waiter_complete_from_same_thread();
  test_waiter_nested();
  test_waiter_complete_via_invoke();
  test_waiter_quit_from_nested_exec_nested_count();

  MainThread::exec();

  // --- Report ---------------------------------------------------------
  if (g_failed.load()) {
    lsError() << LogStream::Color::Red << "core: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }

  int total = g_total.load();
  if (total != 182) {
    lsError() << "core: " << total << '/' << 182;
    return -1;
  }
  lsNotice() << "core: " << total << " checks passed";
  return 0;
}
