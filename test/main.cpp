// Tests for classes under main/.
// Groups:
//   A. ThreadPool          — destroy thread from its own task.
//   B. Coroutine           — suspend/resume, wait, coInvoke (basic).
//   C. FileSystemWatcher   — shared dir watch, file lifecycle, delete+recreate.
//   D. Coroutine — TST     — coInvoke copy/move semantics for value types.
//
// Threading layout:
//   - ThreadPool lives on a dedicated `poolOwner` thread so that its
//     internal sync-invokes (from workers back to the pool owner) are
//     served while the main thread is blocked in wait_for().
//   - Coroutine tests spin up their own AsyncFw::Thread, so resume_queued
//     has a live event loop to land in.
//   - FileSystemWatcher runs on MainThread::exec() and must run last: the
//     first MainThread::exit() permanently finishes Main's event loop, and
//     any later invokeTask() would silently return false.
//

#ifndef _WIN32
  #include <pthread.h>
  #include <signal.h>
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <AsyncFw/ThreadPool>
#include <AsyncFw/Coroutine>
#include <AsyncFw/Thread>
#include <AsyncFw/Timer>
#include <AsyncFw/MainThread>
#include <AsyncFw/FileSystemWatcher>
#include <AsyncFw/File>
#include <AsyncFw/Rrd>
#include <AsyncFw/LogStream>

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
// TST — probe object for copy/move semantics verification (mirrors core.cpp).
// Kept independent from core.cpp because test binaries are separate; sharing
// would require a common header, which is deliberately avoided.
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
inline bool isMovedFrom(const TST &t) { return t.val == -1; }

std::atomic<int> TST::copies_ {0};
std::atomic<int> TST::moves_ {0};
std::atomic<int> TST::live_ {0};

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
// Group A. ThreadPool
// ============================================================================

// Thread::destroy() must be safe to call from within a task running on that
// same thread. Internally destroy() sync-invokes back to the pool's owning
// thread, so the pool must live on a separate AsyncFw::Thread whose exec()
// is running while the main thread waits here.
//
// Note: createThread() already schedules start() in the pool owner thread;
// calling start() again from here would log "thread already running".
void test_threadpool_destroy_from_own_task() {
  ThreadPool *pool = ThreadPool::instance();
  EXPECT(pool != nullptr);

  std::atomic<bool> reachedEnd {false};

  AbstractThreadPool::Thread *t = pool->createThread("DestroyFromOwnTask");
  t->invoke([t, &reachedEnd]() {
    t->destroy();
    // Must not touch t after destroy() — it may already be scheduled for
    // deletion by the pool's owner thread.
    reachedEnd = true;
  });

  EXPECT(wait_for([&] { return reachedEnd.load(); }));
  EXPECT(wait_for([&] { return pool->threads()->empty(); }));
}

// ============================================================================
// Group B. Coroutine — basics
// ============================================================================

// Basic co_await with synchronous resume: control returns to the caller
// frame and the coroutine body completes without any external event source.
void test_coroutine_basic_suspend_resume() {
  std::atomic<int> steps {0};

  auto coro = [&]() -> CoroutineTask {
    ++steps;
    co_await CoroutineAwait<void>([](CoroutineHandle h) {
      h.resume();  // resume immediately, still inside await_suspend
    });
    ++steps;
  };

  coro();
  EXPECT(steps.load() == 2);
}

// CoroutineTask::wait() must block until return_void() has run.
// The coroutine is created inside `t`, so Timer::single lands on `t` and
// its nested exec (spawned by CoroutineTask::wait) serves the resume.
void test_coroutine_wait_blocks_until_done() {
  Thread t("coro-wait");
  t.start();

  std::atomic<bool> completed {false};
  std::atomic<bool> waitReturned {false};

  t.invoke([&]() {
    auto coro = [&]() -> CoroutineTask {
      co_await CoroutineAwait<void>([](CoroutineHandle h) { Timer::single(20, [h]() { h.resume(); }); });
      completed = true;
    };
    CoroutineTask ct = coro();
    ct.wait();
    waitReturned = true;
  }, true);

  EXPECT(completed.load());
  EXPECT(waitReturned.load());
}

// coInvoke must return the callable's value back through co_await.
// The coroutine is created and awaited inside its own thread so that
// resume_queued has a live event loop to post the continuation into.
void test_coroutine_coInvoke_returns_value() {
  Thread t("coro-invoke-val");
  t.start();

  std::atomic<int> result {0};
  std::atomic<bool> finished {false};

  t.invoke([&]() {
    auto coro = [&]() -> CoroutineTask {
      int r = co_await coInvoke([]() -> int { return 42; });
      result = r;
      finished = true;
    };
    coro().wait();
  });

  EXPECT(wait_for([&] { return finished.load(); }));
  EXPECT(result.load() == 42);
}

// coInvoke must handle void-returning callables.
void test_coroutine_coInvoke_void() {
  Thread t("coro-invoke-void");
  t.start();

  std::atomic<bool> ranCallable {false};
  std::atomic<bool> finished {false};

  t.invoke([&]() {
    auto coro = [&]() -> CoroutineTask {
      co_await coInvoke([&]() { ranCallable = true; });
      finished = true;
    };
    coro().wait();
  });

  EXPECT(wait_for([&] { return finished.load(); }));
  EXPECT(ranCallable.load());
}

// ============================================================================
// Group C'. FileSystemWatcher — IN_CLOSE_WRITE bypasses debounce (characterization)
// ============================================================================
//
// Contract being pinned here (verified against linux/FileSystemWatcher.cpp):
//
//   IN_MODIFY / IN_ATTRIB  → append_()  → we_[]        → debounce(1000ms) → Changed
//   IN_CLOSE_WRITE         → notify(Changed) IMMEDIATELY + remove_(w)     → Changed
//
// So a single write() produces TWO inotify events on the watched file
// (IN_MODIFY and then IN_CLOSE_WRITE), and IN_CLOSE_WRITE emits Changed
// right away while cancelling the pending debounce for that Watch*.
//
// Consequences pinned by this test:
//
//   1. Two separate write() calls -> two Changed notifications, NOT one.
//      The debounce window does not coalesce across close-write boundaries.
//
//   2. Recreating the Watch* between the two write() calls (removePath +
//      addPath) does NOT produce a duplicate Changed. removePath() calls
//      remove_(w), which cancels any pending debounce entry for the old
//      Watch*, and addPath() allocates a fresh Watch* whose subsequent
//      IN_CLOSE_WRITE fires exactly once. If a regression ever introduced
//      a dangling entry in we_[] (pointer-vs-key comparison bug), the
//      debounce timer would notify a freed Watch* and either crash under
//      ASan or emit an extra Changed here.
//
//   3. The debounce timer itself fires with an empty we_[] because both
//      IN_MODIFY entries were cancelled by their matching IN_CLOSE_WRITE.
//      It must not emit anything at that point.
void test_filesystemwatcher_recreate_in_debounce_window() {
  const std::string dir = "/tmp/AsyncFw-test-FileSystemWatcher-recreate";
  const std::string f = dir + "/recreate.txt";

  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  // Create the file first so addPath() takes the "file exists" branch
  // (file watch). The directory-watch path is a different state machine
  // and is already covered by test_filesystemwatcher_scenarios().
  {
    File _f {f};
    _f.open(std::ios::binary | std::ios::out);
  }

  FileSystemWatcher watcher;
  std::atomic<int> changedCount {0};
  FunctionConnectionGuard g = watcher.notify.connect([&](const std::string &name, int event) {
    lsInfo() << "recreate-notify:" << name << event;
    if (name == f && event == 0) ++changedCount;  // 0 = Changed
  });

  watcher.addPath(f);  // w1, file watch

  // First write: IN_MODIFY(w1) -> append_(w1); IN_CLOSE_WRITE(w1) -> Changed #1.
  {
    File _f {f};
    _f.open(std::ios::binary | std::ios::app);
    _f.write("x", 1);
  }

  // Give the poll-task a chance to drain IN_MODIFY(w1) + IN_CLOSE_WRITE(w1)
  // before we recreate the watch. Poll runs on Main; we are on Main, so we
  // yield via Timer::single(50). 50ms < debounce(1000ms) — the debounce timer
  // is still armed when we recreate, which is exactly the window we want.
  Timer::single(50, [&, f]() {
    // Recreate w1 -> w2 inside the debounce window.
    // removePath(w1) must cancel w1's pending debounce entry (remove_(w1)).
    // addPath() must allocate a fresh Watch* (w2) with the same key.
    watcher.removePath(f);
    watcher.addPath(f);

    // Second write on w2: IN_MODIFY(w2) -> append_(w2);
    // IN_CLOSE_WRITE(w2) -> Changed #2. No duplicate from w1.
    {
      File _f {f};
      _f.open(std::ios::binary | std::ios::app);
      _f.write("y", 1);
    }
  });

  // After debounce(1000ms) from the second write, we_ must be empty
  // (both IN_MODIFY entries were cancelled by their IN_CLOSE_WRITE).
  // Give some margin so we also catch a hypothetical late fire.
  Timer::single(1500, [&]() {
    lsNotice() << "recreate-in-debounce: Changed(f) count =" << changedCount.load();
    EXPECT_MSG(changedCount.load() == 2, "Changed(f) count=" + std::to_string(changedCount.load()) +
                                             " (expected 2: one per write() — IN_CLOSE_WRITE bypasses debounce "
                                             "and emits immediately, cancelling the pending IN_MODIFY entry; "
                                             "a count >2 would mean a dangling Watch* in we_[] emitted an "
                                             "extra Changed; a count <2 would mean a lost close-write event)");
    MainThread::exit();
  });

  MainThread::exec();
}

// FileSystemWatcher

// Scenario A: shared directory watch.
//   f1 and f2 are both pending (do not exist yet), so addPath() registers
//   a single shared directory watch for both. removePath(f1) must NOT tear
//   down the shared watch while f2 still depends on it.
//   After creating f2 the watcher must still report Created(f2).
//
// Scenario B: file watch lifecycle.
//   addPath(f3) on an existing file registers an exclusive file watch.
//   removePath(f3) must release it; re-addPath(f3) must register a fresh
//   one; a single write must then produce exactly one Changed(f3).
//
// Scenario C: delete + recreate.
//   f4 exists, is watched, gets removed, then recreated. Notify must fire
//   for the removed and re-created states without losing the watch.
void test_filesystemwatcher_scenarios() {
  const std::string dir = "/tmp/AsyncFw-test-FileSystemWatcher";
  const std::string f1 = dir + "/file1.txt";
  const std::string f2 = dir + "/file2.txt";
  const std::string f3 = dir + "/file3.txt";
  const std::string f4 = dir + "/file4.txt";

  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  FileSystemWatcher watcher;
  int phase = 0;
  std::atomic<bool> scenarioAOk {false};
  std::atomic<int> scenarioBNotify {0};
  std::atomic<int> scenarioCRemoved {0};
  std::atomic<int> scenarioCCreated {0};

  FunctionConnectionGuard g = watcher.notify.connect([&](const std::string &name, int event) {
    lsInfo() << "notify:" << name << event;
    // event: -1 = Removed, 0 = Changed, 1 = Created.
    if (phase == 0 && name == f2 && event == 1) { scenarioAOk = true; }
    if (phase == 1 && name == f3 && event == 0) { ++scenarioBNotify; }
    if (name == f4 && event == -1) ++scenarioCRemoved;
    if (name == f4 && event == 1) ++scenarioCCreated;
  });

  // ---- Scenario A -----------------------------------------------------
  watcher.addPath(f1);
  watcher.addPath(f2);
  watcher.removePath(f1);
  {
    File _f {f2};
    _f.open(std::ios::binary | std::ios::out);
  }

  // ---- Scenario B (deferred) ------------------------------------------
  Timer::single(300, [&, f3]() {
    phase = 1;
    {
      File _f {f3};
      _f.open(std::ios::binary | std::ios::out);
    }
    watcher.addPath(f3);
    watcher.removePath(f3);
    watcher.addPath(f3);
    {
      File _f {f3};
      _f.open(std::ios::binary | std::ios::out | std::ios::app);
      _f.write("b", 1);
    }
  });

  // ---- Scenario C (delete + recreate) ---------------------------------
  {
    File _f {f4};
    _f.open(std::ios::binary | std::ios::out);
  }
  watcher.addPath(f4);
  std::filesystem::remove(f4);
  Timer::single(300, [f4]() {
    File _f {f4};
    _f.open(std::ios::binary | std::ios::out);
  });

  // ---- Final report ---------------------------------------------------
  Timer::single(1500, [&]() {
    lsNotice() << "Scenario A:" << (scenarioAOk.load() ? "OK" : "BUG (Created(f2) not received)");
    lsNotice() << "Scenario B: Changed(f3) count =" << scenarioBNotify.load();
    lsNotice() << "Scenario C: Removed(f4) =" << scenarioCRemoved.load() << "Created(f4) =" << scenarioCCreated.load();

    EXPECT(scenarioAOk.load());
    EXPECT(scenarioBNotify.load() == 1);
    EXPECT(scenarioCRemoved.load() == 1);
    EXPECT(scenarioCCreated.load() == 1);

    MainThread::exit();
  });

  MainThread::exec();
}

// ============================================================================
// Group D. Coroutine — TST (copy/move semantics of coInvoke)
// ============================================================================
//
// coInvoke is single-shot dispatch to exactly one callable. Its argument
// handling is deliberately minimal — no intermediate tuple copy, no apply
// move:
//
//   coInvoke(f, args...) forwards its arguments into
//   CoroutineInvokeAwait<R(Args...)> with `Args` deduced as forwarding-refs:
//     - lvalue arg  → Args = TST&   → tuple<TST&>   holds a *reference*
//     - xvalue arg  → Args = TST&&  → tuple<TST&&>  holds a *reference*
//
//   At resume, await_suspend does std::apply(*f_, std::move(a_)). Because
//   the tuple elements are references, std::move(a_) yields the same
//   lvalue/xvalue reference — the reference itself is moved, not the TST.
//   The callable's value-parameter is then initialized directly from that
//   reference, performing exactly one copy (lvalue) or one move (xvalue).
//
// Net cost (single value-param callable):
//   lvalue arg: 1 copy  (into callable value-param); caller intact.
//   xvalue arg: 1 move  (into callable value-param); caller moved-from.
//
// IMPORTANT — lifetime contract:
//   The tuple holds a reference, not an owning copy. The argument MUST stay
//   alive until the co_await completes. This is fine in normal usage where
//   the caller does `co_await coInvoke(f, arg)` and `arg` is a local that
//   lives across the await; it is NOT safe to hand coInvoke a temporary
//   that would outlive the enclosing scope before resume_queued delivers
//   the continuation.
//
//   This is a deliberate single-shot optimization — unlike
//   FunctionConnector multicast, where any intermediate move would
//   move-steal from subsequent subscribers (see the CRITICAL WARNING in
//   core/invocable.hpp). Any change to this contract MUST be reflected
//   here in the counters below.
//
// Return-by-value: promise().setData(std::any(result)) copies/moves into
// the std::any; data<R>() returns by value (another copy). Treated as part
// of the dispatch cost, not a leak — liveDelta still returns to baseline.

// coInvoke to the global ThreadPool, lvalue TST argument.
// The caller's TST must not become moved-from: the tuple holds a reference,
// and only the callable's value-param takes a copy.
void test_coroutine_coInvoke_pool_lvalue_tst() {
  TSTMeter meter;
  {
    std::atomic<bool> finished {false};
    std::atomic<int> seenVal {-1};

    Thread driver("co-pool-lv");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST arg {7};
        int c0 = TST::copies_.load();
        int m0 = TST::moves_.load();
        co_await coInvoke([&](TST t) { seenVal = t.val; }, arg);
        int c = TST::copies_.load() - c0;
        int m = TST::moves_.load() - m0;
        // lvalue arg: 1 copy into the callable's value-param.
        // Zero moves: tuple<TST&> only holds a reference, caller untouched.
        EXPECT_MSG(c == 1, "copies delta=" + std::to_string(c) + " (expected 1: value-param copy)");
        EXPECT_MSG(m == 0, "moves delta=" + std::to_string(m) + " (expected 0: caller intact)");
        EXPECT(!isMovedFrom(arg));
        EXPECT(arg.val == 7);
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(seenVal.load() == 7);
    driver.invoke([]() {}, true);  // barrier before ~driver
  }
  EXPECT(meter.liveDelta() == 0);
}

// coInvoke to the global ThreadPool, xvalue TST argument.
// The caller's object becomes moved-from (value-param move); the tuple
// itself only holds a reference, so no additional move is produced.
void test_coroutine_coInvoke_pool_xvalue_tst() {
  TSTMeter meter;
  {
    std::atomic<bool> finished {false};
    std::atomic<int> seenVal {-1};

    Thread driver("co-pool-xv");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST arg {7};
        int c0 = TST::copies_.load();
        int m0 = TST::moves_.load();
        co_await coInvoke([&](TST t) { seenVal = t.val; }, std::move(arg));
        int c = TST::copies_.load() - c0;
        int m = TST::moves_.load() - m0;
        // xvalue arg: 1 move into the callable's value-param.
        // Zero copies: tuple<TST&&> only holds a reference.
        EXPECT_MSG(c == 0, "copies delta=" + std::to_string(c) + " (expected 0)");
        EXPECT_MSG(m == 1, "moves delta=" + std::to_string(m) + " (expected 1: value-param move)");
        EXPECT(isMovedFrom(arg));
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(seenVal.load() == 7);
    driver.invoke([]() {}, true);
  }
  EXPECT(meter.liveDelta() == 0);
}

// coInvoke to a specific AsyncFw::Thread, lvalue TST argument.
// Same copy/move contract as the pool overload; the only difference is the
// target thread. Also verifies that the callable ran on `target`.
void test_coroutine_coInvoke_thread_lvalue_tst() {
  TSTMeter meter;
  {
    Thread target("co-target-lv");
    target.start();

    std::atomic<bool> finished {false};
    std::atomic<int> seenVal {-1};
    std::atomic<std::thread::id> observed {};
    const std::thread::id expected = target.id();

    Thread driver("co-driver-lv");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST arg {7};
        int c0 = TST::copies_.load();
        int m0 = TST::moves_.load();
        co_await coInvoke(&target, [&](TST t) {
          seenVal = t.val;
          observed = std::this_thread::get_id();
        }, arg);
        int c = TST::copies_.load() - c0;
        int m = TST::moves_.load() - m0;
        EXPECT_MSG(c == 1, "copies delta=" + std::to_string(c) + " (expected 1: value-param copy)");
        EXPECT_MSG(m == 0, "moves delta=" + std::to_string(m) + " (expected 0: caller intact)");
        EXPECT(!isMovedFrom(arg));
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(seenVal.load() == 7);
    EXPECT(observed.load() == expected);
    driver.invoke([]() {}, true);
  }
  EXPECT(meter.liveDelta() == 0);
}

// coInvoke to a specific AsyncFw::Thread, xvalue TST argument.
void test_coroutine_coInvoke_thread_xvalue_tst() {
  TSTMeter meter;
  {
    Thread target("co-target-xv");
    target.start();

    std::atomic<bool> finished {false};
    std::atomic<int> seenVal {-1};

    Thread driver("co-driver-xv");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST arg {7};
        int c0 = TST::copies_.load();
        int m0 = TST::moves_.load();
        co_await coInvoke(&target, [&](TST t) { seenVal = t.val; }, std::move(arg));
        int c = TST::copies_.load() - c0;
        int m = TST::moves_.load() - m0;
        EXPECT_MSG(c == 0, "copies delta=" + std::to_string(c) + " (expected 0)");
        EXPECT_MSG(m == 1, "moves delta=" + std::to_string(m) + " (expected 1: value-param move)");
        EXPECT(isMovedFrom(arg));
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(seenVal.load() == 7);
    driver.invoke([]() {}, true);
  }
  EXPECT(meter.liveDelta() == 0);
}

// coInvoke returning TST by value. The result travels through
// promise().setData(std::any) and data<TST>(), which copies/moves into the
// std::any (and back out). We only assert semantic correctness: the value
// round-trips intact and no TST leaks. Exact copy counts are intentionally
// not pinned — they depend on std::any internals, not on coInvoke logic.
void test_coroutine_coInvoke_returns_tst() {
  TSTMeter meter;
  {
    std::atomic<bool> finished {false};
    std::atomic<int> resultVal {-1};

    Thread driver("co-return-tst");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST r = co_await coInvoke([]() -> TST { return TST {42}; });
        resultVal = r.val;
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(resultVal.load() == 42);
    driver.invoke([]() {}, true);
  }
  EXPECT(meter.liveDelta() == 0);
}

// coInvoke where the callable captures a TST by value (mirrors the
// example/CoroutineInvoke pattern). The capture must survive the await.
void test_coroutine_coInvoke_lambda_capture_tst() {
  TSTMeter meter;
  {
    std::atomic<bool> finished {false};
    std::atomic<int> captureSeenVal {-1};
    std::atomic<int> argSeenVal {-1};

    Thread driver("co-cap-tst");
    driver.start();
    driver.invoke([&]() {
      auto coro = [&]() -> CoroutineTask {
        TST cap {111};
        auto lambda = [&, cap](TST t) {
          captureSeenVal = cap.val;
          argSeenVal = t.val;
        };
        TST arg {42};
        co_await coInvoke(lambda, arg);
        EXPECT(!isMovedFrom(cap));
        EXPECT(cap.val == 111);
        EXPECT(!isMovedFrom(arg));
        finished = true;
      };
      coro().wait();
    });

    EXPECT(wait_for([&] { return finished.load(); }));
    EXPECT(captureSeenVal.load() == 111);
    EXPECT(argSeenVal.load() == 42);
    driver.invoke([]() {}, true);
  }
  EXPECT(meter.liveDelta() == 0);
}

// ============================================================================
// Group E. Rrd — count()/first_/read()/readFromArray with sequential and
// sparse fills.
// ============================================================================
//
// count_() = (first_ == 0) ? 0 : last_ - first_ + 1.
// readFromArray(i) = dataBase[(1 + last_ + i - count_()) % dbSize]
//                  = dataBase[(first_ + i) % dbSize]   (identical).
//
// Invariants pinned here:
//   Sequential auto-index (Log):    first_ = 1, count_ = min(last_, dbSize)
//   Sequential wrapped:             first_ = last_ - dbSize + 1, count_ = dbSize
//   Sparse explicit (RrdClient sync): first_ = first_appended_pos
//   read(index < first_) must clamp to first_, NOT return empty prefix
//   clear(): first_ = last_ = 0
//   Rrd(0, fn)  → readOnly view; file must not be overwritten

static std::string rrdItemStr(const Rrd::Item &it) { return std::string(it.begin(), it.end()); }

// Baseline: auto-index appends keep count == number of appends.
void test_rrd_sequential_append() {
  Rrd rrd(10);
  EXPECT(rrd.count() == 0);
  EXPECT(rrd.lastIndex() == 0);

  for (int i = 1; i <= 5; ++i) rrd.append(DataArray(std::to_string(i)));

  EXPECT(rrd.count() == 5);
  EXPECT(rrd.lastIndex() == 5);
  for (int i = 0; i < 5; ++i) { EXPECT_MSG(rrdItemStr(rrd.readFromArray(i)) == std::to_string(i + 1), "readFromArray(" + std::to_string(i) + ")"); }
}

// Baseline: ring wrap caps count at dbSize and keeps the newest dbSize items.
void test_rrd_sequential_wrap() {
  Rrd rrd(10);
  for (int i = 1; i <= 15; ++i) rrd.append(DataArray(std::to_string(i)));

  EXPECT(rrd.count() == 10);
  EXPECT(rrd.lastIndex() == 15);
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "6", "readFromArray(0)=" + rrdItemStr(rrd.readFromArray(0)));
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(9)) == "15", "readFromArray(9)=" + rrdItemStr(rrd.readFromArray(9)));
}

// clear() resets first_ and last_ together.
void test_rrd_clear() {
  Rrd rrd(10);
  for (int i = 1; i <= 5; ++i) rrd.append(DataArray(std::to_string(i)));
  EXPECT(rrd.count() == 5);

  rrd.clear();
  EXPECT(rrd.count() == 0);
  EXPECT(rrd.lastIndex() == 0);

  for (int i = 1; i <= 3; ++i) rrd.append(DataArray(std::to_string(100 + i)));
  EXPECT(rrd.count() == 3);
  EXPECT(rrd.lastIndex() == 3);
  EXPECT(rrdItemStr(rrd.readFromArray(0)) == "101");
  EXPECT(rrdItemStr(rrd.readFromArray(2)) == "103");
}

// read() clamp: index < first_ must clamp to first_, not return empty prefix.
// Pre-fix, read(list, 3101) with first_=4001/last_=4100 returned 1000 items
// (900 empty + 100 real); post-fix returns just the 100 real ones.
void test_rrd_read_clamp_to_first() {
  Rrd rrd(1000, 1, 0);  // interval_ = 1 → explicit index allowed
  for (uint64_t i = 0; i < 100; ++i) { rrd.append(DataArray(std::to_string(4001 + i)), 4001 + i); }
  EXPECT(rrd.count() == 100);
  EXPECT(rrd.lastIndex() == 4100);

  DataArrayList list;
  uint64_t lastRead = rrd.read(&list, 3101, 0, nullptr);

  EXPECT_MSG(list.size() == 100, "list.size()=" + std::to_string(list.size()));
  EXPECT_MSG(lastRead == 4100, "lastRead=" + std::to_string(lastRead));
  if (list.size() == 100) {
    EXPECT_MSG(rrdItemStr(list[0]) == "4001", "list[0]=\"" + rrdItemStr(list[0]) + "\"");
    EXPECT_MSG(rrdItemStr(list[99]) == "4100", "list[99]=\"" + rrdItemStr(list[99]) + "\"");
  }
}

// Save/load roundtrip for a contiguous wrapped Log. readFromFile must
// reconstruct first_ = last_ - dbSize + 1 for full ring. Works today.
void test_rrd_file_roundtrip_contiguous() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-contiguous.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(1000, fn);
    for (int i = 1; i <= 5000; ++i) rrd.append(DataArray(std::to_string(i)));
    EXPECT(rrd.count() == 1000);
    EXPECT(rrd.lastIndex() == 5000);
    rrd.save();
  }

  {
    Rrd rrd(1000, fn);
    EXPECT(rrd.lastIndex() == 5000);
    EXPECT_MSG(rrd.count() == 1000, "count() after reload=" + std::to_string(rrd.count()));
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "4001", "readFromArray(0)=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(999)) == "5000", "readFromArray(999)=\"" + rrdItemStr(rrd.readFromArray(999)) + "\"");
  }

  std::filesystem::remove(fn);
}

// Rrd(0, fn) is a read-only view: it must load the existing file, set
// dbSize from the payload, and NOT overwrite the file on destruction.
void test_rrd_readonly_load_contiguous() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-readonly.db";
  std::filesystem::remove(fn);

  {
    Rrd writer(1000, fn);
    for (int i = 1; i <= 5000; ++i) writer.append(DataArray(std::to_string(i)));
    writer.save();
  }

  const auto sizeBefore = std::filesystem::file_size(fn);

  {
    Rrd view(0, fn);  // readOnly
    EXPECT(view.count() == 1000);
    EXPECT(view.lastIndex() == 5000);
    EXPECT_MSG(rrdItemStr(view.readFromArray(0)) == "4001", "readFromArray(0) in read-only view");
    EXPECT_MSG(rrdItemStr(view.readFromArray(999)) == "5000", "readFromArray(999) in read-only view");
  }

  const auto sizeAfter = std::filesystem::file_size(fn);
  EXPECT_MSG(sizeBefore == sizeAfter, "file was modified by ~Rrd: before=" + std::to_string(sizeBefore) + " after=" + std::to_string(sizeAfter) + " (Rrd(0, fn) must be read-only)");

  std::filesystem::remove(fn);
}

// Contract for interval_ == 0 (auto-index mode):
//   - empty DB (last_ == 0): any explicit index is allowed (seeds first_/last_,
//     used by RrdClient to start from an arbitrary server position);
//   - non-empty DB: explicit index must be last_ + 1 (sequential only).
void test_rrd_append_explicit_index_auto_mode() {
  Rrd rrd(1000);  // interval_ == 0

  // 1. Empty DB: any explicit index is allowed.
  rrd.append(DataArray("5000"), 5000);
  EXPECT(rrd.count() == 1);
  EXPECT(rrd.lastIndex() == 5000);
  EXPECT(rrdItemStr(rrd.readFromArray(0)) == "5000");

  // 2. Non-empty DB, sequential explicit index: allowed.
  rrd.append(DataArray("5001"), 5001);
  EXPECT(rrd.count() == 2);
  EXPECT(rrd.lastIndex() == 5001);
  EXPECT(rrdItemStr(rrd.readFromArray(1)) == "5001");

  // 3. Non-empty DB, forward jump: rejected.
  rrd.append(DataArray("bad-forward"), 6000);
  EXPECT(rrd.count() == 2);
  EXPECT(rrd.lastIndex() == 5001);

  // 4. Non-empty DB, duplicate: rejected.
  rrd.append(DataArray("bad-duplicate"), 5001);
  EXPECT(rrd.count() == 2);
  EXPECT(rrd.lastIndex() == 5001);

  // 5. Non-empty DB, rewind: rejected.
  rrd.append(DataArray("bad-rewind"), 5000);
  EXPECT(rrd.count() == 2);
  EXPECT(rrd.lastIndex() == 5001);

  // 6. Auto-index after explicit: continues from last_ + 1.
  rrd.append(DataArray("5002"));
  EXPECT(rrd.count() == 3);
  EXPECT(rrd.lastIndex() == 5002);
  EXPECT(rrdItemStr(rrd.readFromArray(2)) == "5002");
}

// Auto-index contiguous fill: count == min(lastIndex, dbSize).
// This is exactly the RrdClient-after-migration pattern: client dbSize=1000,
// server at 5000, client receives last 1000 records and appends them without
// an explicit index; server position is tracked in RrdClient::lastTime, not
// in Rrd::lastIndex.
void test_rrd_append_auto_index_matches_rrdclient() {
  Rrd client(1000);

  const uint64_t serverFirst = 4001;
  const uint64_t serverLast = 5000;
  for (uint64_t p = serverFirst; p <= serverLast; ++p) { client.append(DataArray(std::to_string(p))); }

  EXPECT(client.count() == 1000);
  EXPECT_MSG(client.lastIndex() == 1000, "lastIndex=" + std::to_string(client.lastIndex()) + " (local counter, not server position)");
  EXPECT_MSG(rrdItemStr(client.readFromArray(0)) == std::to_string(serverFirst), "readFromArray(0)=\"" + rrdItemStr(client.readFromArray(0)) + "\"");
  EXPECT_MSG(rrdItemStr(client.readFromArray(999)) == std::to_string(serverLast), "readFromArray(999)=\"" + rrdItemStr(client.readFromArray(999)) + "\"");
}

// interval > 0 allows explicit index. Gaps (fill == 0) are cleared but remain
// inside the addressable range [first_, last_]; count() covers the full span,
// consumers filter empty items.
void test_rrd_append_explicit_index_with_interval() {
  Rrd rrd(1000, 1, 0);  // interval = 1, fill = 0
  rrd.append(DataArray("100"), 100);
  rrd.append(DataArray("200"), 200);
  rrd.append(DataArray("400"), 400);

  EXPECT(rrd.lastIndex() == 400);
  EXPECT_MSG(rrd.count() == 301, "count=" + std::to_string(rrd.count()) + " (span [first_=100, last_=400] includes cleared gaps)");
  EXPECT(rrdItemStr(rrd.readFromArray(0)) == "100");
  EXPECT(rrdItemStr(rrd.readFromArray(1)) == "");  // gap 101..199
  EXPECT(rrdItemStr(rrd.readFromArray(100)) == "200");
  EXPECT(rrdItemStr(rrd.readFromArray(101)) == "");  // gap 201..399
  EXPECT(rrdItemStr(rrd.readFromArray(300)) == "400");
}

// Save/load for interval > 0 with sparse positions: first_ is persisted
// (saveToFile writes it when interval_ || fill), reload preserves exact layout.
void test_rrd_file_roundtrip_sparse_time_based() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-sparse-time.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(1000, 1, 0, fn);
    rrd.append(DataArray("100"), 100);
    rrd.append(DataArray("200"), 200);
    rrd.append(DataArray("400"), 400);
    EXPECT(rrd.count() == 301);
    EXPECT(rrd.lastIndex() == 400);
    rrd.save();
  }

  {
    Rrd rrd(1000, 1, 0, fn);
    EXPECT_MSG(rrd.lastIndex() == 400, "lastIndex after reload=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrd.count() == 301, "count after reload=" + std::to_string(rrd.count()));
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "100", "readFromArray(0) after reload");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(1)) == "", "readFromArray(1) after reload (gap)");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(100)) == "200", "readFromArray(100) after reload");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(300)) == "400", "readFromArray(300) after reload");
  }

  std::filesystem::remove(fn);
}

// Regression: sparse positions >= dbSize. Without first_ persisted, the
// wrap fallback (last_ - dbSize + 1) understates first_ for sparse fills
// that started above dbSize. With first_ stored, the exact position is
// preserved.
void test_rrd_file_roundtrip_sparse_offset() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-sparse-offset.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(1000, 1, 0, fn);  // interval_ > 0, dbSize = 1000
    rrd.append(DataArray("1001"), 1001);
    rrd.append(DataArray("1101"), 1101);
    rrd.append(DataArray("1301"), 1301);
    EXPECT(rrd.count() == 301);
    EXPECT(rrd.lastIndex() == 1301);
    rrd.save();
  }

  {
    Rrd rrd(1000, 1, 0, fn);
    EXPECT_MSG(rrd.lastIndex() == 1301, "lastIndex after reload=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrd.count() == 301, "count after reload=" + std::to_string(rrd.count()) + " (sparse positions > dbSize: first_ must be 1001, not last_ - dbSize + 1 = 302)");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "1001", "readFromArray(0) after reload=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(200)) == "", "readFromArray(200) after reload=\"" + rrdItemStr(rrd.readFromArray(200)) + "\" (gap at position 1201)");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(300)) == "1301", "readFromArray(300) after reload=\"" + rrdItemStr(rrd.readFromArray(300)) + "\"");
  }

  std::filesystem::remove(fn);
}

// Edge: single record at position == dbSize. Without first_ persisted,
// wrap fallback gives first_ = dbSize - dbSize + 1 = 1 (off-by-(dbSize-1)).
void test_rrd_file_roundtrip_first_equals_dbsize() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-first-eq-db.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(10, 1, 0, fn);
    rrd.append(DataArray("data"), 10);  // pos == dbSize
    EXPECT(rrd.count() == 1);
    EXPECT(rrd.lastIndex() == 10);
    rrd.save();
  }

  {
    Rrd rrd(10, 1, 0, fn);
    EXPECT_MSG(rrd.count() == 1, "count=" + std::to_string(rrd.count()) + " (was: fallback wrap = 10)");
    EXPECT_MSG(rrd.lastIndex() == 10, "lastIndex=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "data", "readFromArray(0)=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
  }

  std::filesystem::remove(fn);
}

// Regression: interval_ > 0, fill == 0, first_ falls into a cleared gap after
// wrap. Without first_ persisted in the file, scan-based reconstruction of
// readFromFile() picked the only non-empty slot (position last_), collapsing
// count() to 1.
//
// Scenario (matches example/misc/main.cpp repro):
//   Rrd(10, 1, 0, fn):
//     append("A", 1)   -> first_=1,  last_=1,  slot 1 = "A"
//     append("B", 21)  -> first_=12, last_=21, slot 1 = "B" ("A" overwritten
//                         by wrap), slots 2..9 and 0 cleared (gap 12..20)
//   pre-save count() = 21 - 12 + 1 = 10.
//
// With the fix, saveToFile() writes first_ for interval_ > 0 and readFromFile()
// restores it; without the fix, scan picks first_ = 21 -> count() = 1.
//
// No MainThread::exec() involved — pure save/reload.
void test_rrd_first_persisted_sparse() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-first-sparse.db";
  std::filesystem::remove(fn);

  // ---- Phase 1: build sparse DB and save -----------------------------
  {
    Rrd rrd(10, 1, 0, fn);
    rrd.append(DataArray("A"), 1);
    rrd.append(DataArray("B"), 21);

    EXPECT_MSG(rrd.lastIndex() == 21, "pre-save lastIndex=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrd.count() == 10, "pre-save count=" + std::to_string(rrd.count()));
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "", "pre-save readFromArray(0) (gap at position 12), got \"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(9)) == "B", "pre-save readFromArray(9) (position 21), got \"" + rrdItemStr(rrd.readFromArray(9)) + "\"");
    rrd.save();
  }

  // ---- Phase 2: reload (read-write) ----------------------------------
  {
    Rrd rrd(10, 1, 0, fn);
    EXPECT_MSG(rrd.lastIndex() == 21, "lastIndex after reload=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrd.count() == 10, "count after reload=" + std::to_string(rrd.count()) +
                                      " (expected 10; without first_ persisted, scan-based reconstruction "
                                      "picks first_=21 because only slot 1 (= position 21) is non-empty)");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "", "readFromArray(0) after reload (gap at position 12), got \"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(9)) == "B", "readFromArray(9) after reload (position 21), got \"" + rrdItemStr(rrd.readFromArray(9)) + "\"");
  }

  // ---- Phase 3: reload (read-only, dbSize taken from the file) -------
  // Rrd(0, fn) must not modify the file and must restore first_ too.
  const auto sizeBefore = std::filesystem::file_size(fn);
  {
    Rrd view(0, fn);  // readOnly
    EXPECT_MSG(view.lastIndex() == 21, "read-only lastIndex=" + std::to_string(view.lastIndex()));
    EXPECT_MSG(view.count() == 10, "read-only count=" + std::to_string(view.count()));
    EXPECT_MSG(rrdItemStr(view.readFromArray(0)) == "", "read-only readFromArray(0) (gap at position 12)");
    EXPECT_MSG(rrdItemStr(view.readFromArray(9)) == "B", "read-only readFromArray(9) (position 21)");
  }
  const auto sizeAfter = std::filesystem::file_size(fn);
  EXPECT_MSG(sizeBefore == sizeAfter, "read-only reload modified the file: before=" + std::to_string(sizeBefore) + " after=" + std::to_string(sizeAfter));

  std::filesystem::remove(fn);
}

// Empty DB after reload: last_ == 0 must not overflow first_.
void test_rrd_reload_empty() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-reload-empty.db";
  std::filesystem::remove(fn);
  {
    Rrd r(10, 0, 0, fn);
    r.save();
  }
  Rrd r(10, 0, 0, fn);
  EXPECT(r.count() == 0);
  EXPECT(r.lastIndex() == 0);
  std::filesystem::remove(fn);
}

// Single item at pos 1 (Log with 1 message).
void test_rrd_reload_single_at_one() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-reload-single1.db";
  std::filesystem::remove(fn);
  {
    Rrd r(1000, 0, 0, fn);
    r.append(DataArray("x"), 1);
    r.save();
  }
  Rrd r(1000, 0, 0, fn);
  EXPECT_MSG(r.count() == 1, "count=" + std::to_string(r.count()));
  EXPECT(r.lastIndex() == 1);
  EXPECT(rrdItemStr(r.readFromArray(0)) == "x");
  std::filesystem::remove(fn);
}

// Single item at pos N > 1 (RrdClient with one server record).
void test_rrd_reload_single_at_high() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-reload-singleN.db";
  std::filesystem::remove(fn);
  {
    Rrd r(1000, 0, 0, fn);
    r.append(DataArray("x"), 5000);
    r.save();
  }
  Rrd r(1000, 0, 0, fn);
  EXPECT_MSG(r.count() == 1, "count=" + std::to_string(r.count()));
  EXPECT(r.lastIndex() == 5000);
  EXPECT(rrdItemStr(r.readFromArray(0)) == "x");
  std::filesystem::remove(fn);
}

// Regression: interval_ > 0, span < dbSize, P > 1. Formula
// max(1, last_ - dbSize + 1) understates first_ because the ring hasn't
// wrapped yet, and scan is not available (interval_ > 0 path only has the
// formula fallback). The only reliable source is first_ persisted in the file.
//
//   Rrd(10, 1, 0, fn):
//     append("x", 5)   -> first_=5,  last_=5
//     append("y", 8)   -> first_=5,  last_=8, span=4 < dbSize=10
//   real count() = 4.
//   Without first_ in file: formula gives first_=1, count()=8 (wrong).
void test_rrd_first_persisted_unwrapped() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-first-unwrapped.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(10, 1, 0, fn);
    rrd.append(DataArray("x"), 5);
    rrd.append(DataArray("y"), 8);
    EXPECT_MSG(rrd.count() == 4, "pre-save count=" + std::to_string(rrd.count()));
    EXPECT_MSG(rrd.lastIndex() == 8, "pre-save lastIndex=" + std::to_string(rrd.lastIndex()));
    rrd.save();
  }

  {
    Rrd rrd(10, 1, 0, fn);
    EXPECT_MSG(rrd.lastIndex() == 8, "lastIndex after reload=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrd.count() == 4, "count after reload=" + std::to_string(rrd.count()) + " (span=4 < dbSize=10; formula would give 8; only persisted first_=5 is correct)");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "x", "readFromArray(0) after reload=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(3)) == "y", "readFromArray(3) after reload=\"" + rrdItemStr(rrd.readFromArray(3)) + "\"");
  }

  std::filesystem::remove(fn);
}

// interval_ == 0 with explicit sequential indices starting at 4001.
// Scan must find first_=4001; the formula would give last_ - dbSize + 1 = 3101.
// Pins the interval_==0 branch: it must use scan, not the formula.
void test_rrd_interval_zero_sparse_start() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-i0-sparse.db";
  std::filesystem::remove(fn);

  {
    Rrd rrd(1000, 0, 0, fn);
    for (uint64_t i = 0; i < 100; ++i) rrd.append(DataArray(std::to_string(4001 + i)), 4001 + i);
    EXPECT_MSG(rrd.count() == 100, "pre-save count=" + std::to_string(rrd.count()));
    EXPECT_MSG(rrd.lastIndex() == 4100, "pre-save lastIndex=" + std::to_string(rrd.lastIndex()));
    rrd.save();
  }

  {
    Rrd rrd(1000, 0, 0, fn);
    EXPECT_MSG(rrd.count() == 100, "count after reload=" + std::to_string(rrd.count()) + " (interval_=0 must use scan; formula would give last_-dbSize+1 = 3101)");
    EXPECT_MSG(rrd.lastIndex() == 4100, "lastIndex after reload=" + std::to_string(rrd.lastIndex()));
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "4001", "readFromArray(0) after reload=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
    EXPECT_MSG(rrdItemStr(rrd.readFromArray(99)) == "4100", "readFromArray(99) after reload=\"" + rrdItemStr(rrd.readFromArray(99)) + "\"");
  }

  std::filesystem::remove(fn);
}

// Build a legacy Rrd file at `fn` with the pre-first_ layout:
//   [last_][dataBase]            (no first_ trailer)
// compressed exactly like Rrd::saveToFile. Read-side should detect the
// missing trailer (DataStream::end() right after >> dataBase) and fall back
// to scan (interval_ == 0) or formula (interval_ > 0). Pins backward
// compatibility of Rrd::readFromFile.
static void writeLegacyRrdFile(const std::string &fn, uint64_t last, const DataArrayList &db) {
  DataStream ds;
  ds << last;
  ds << db;
  DataArray buf = DataArray::compress(ds.array());
  std::ofstream f(fn, std::ios::binary);
  f.write(reinterpret_cast<const char *>(buf.data()), buf.size());
}

// Legacy file, interval_ == 0, ring wrapped (span == dbSize).
// Scan must reconstruct first_ = last_ - dbSize + 1 = 191, matching what
// saveToFile would have written if first_ had been persisted. This is the
// Log-with-many-messages pattern: old Log files (before first_ was added)
// must still load without losing records.
//
// Physical db layout (slot = pos % dbSize):
//   db[0]="200", db[1]="191", db[2]="192", ..., db[9]="199"
//   first_=191, last_=200
void test_rrd_legacy_file_interval0_wrapped() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-legacy-i0-wrap.db";
  std::filesystem::remove(fn);

  DataArrayList db;
  db.resize(10);
  for (uint64_t p = 191; p <= 200; ++p) db[p % 10] = DataArray(std::to_string(p));
  writeLegacyRrdFile(fn, 200, db);

  Rrd rrd(10, 0, 0, fn);  // interval_ == 0 -> scan branch
  EXPECT_MSG(rrd.lastIndex() == 200, "lastIndex=" + std::to_string(rrd.lastIndex()));
  EXPECT_MSG(rrd.count() == 10, "count=" + std::to_string(rrd.count()) +
                                    " (legacy file, no first_: scan must reconstruct first_=191, "
                                    "matching what saveToFile would write for a wrapped ring)");
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "191", "readFromArray(0)=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(9)) == "200", "readFromArray(9)=\"" + rrdItemStr(rrd.readFromArray(9)) + "\"");

  std::filesystem::remove(fn);
}

// Legacy file, interval_ == 0, NOT wrapped (span < dbSize).
// This is where scan is strictly required: formula would give
// last_ - dbSize + 1 = 4001, hiding the real first_ = 5000 and inflating
// count() to 1000. Scan must walk down from last_ until the first empty
// physical slot and stop.
//
// Physical db layout (dbSize=1000, one record at position 5000):
//   db[0]="5000", db[1..999]=empty
//   first_=5000, last_=5000
void test_rrd_legacy_file_interval0_single_high() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-legacy-i0-single.db";
  std::filesystem::remove(fn);

  DataArrayList db;
  db.resize(1000);
  db[5000 % 1000] = DataArray("x");
  writeLegacyRrdFile(fn, 5000, db);

  Rrd rrd(1000, 0, 0, fn);  // interval_ == 0 -> scan branch
  EXPECT_MSG(rrd.lastIndex() == 5000, "lastIndex=" + std::to_string(rrd.lastIndex()));
  EXPECT_MSG(rrd.count() == 1, "count=" + std::to_string(rrd.count()) + " (legacy scan must find first_=5000; formula would inflate to 1000)");
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "x", "readFromArray(0)=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");

  std::filesystem::remove(fn);
}

// Legacy file, interval_ > 0, wrapped. This path has no scan (interval_ > 0
// uses the formula), and for a wrapped ring the formula is exact:
//   first_ = last_ - dbSize + 1 = 200 - 10 + 1 = 191
// Pins the formula fallback for pre-first_ time-based Rrd files.
//
// Note: legacy interval_ > 0 files that were written UNWRAPPED will still
// reload with a wrong first_ (formula understates); this is a documented
// limitation of the fallback, not a regression. New files always carry
// first_, so the limitation only affects pre-upgrade archives.
void test_rrd_legacy_file_interval_positive_wrapped() {
  const std::string fn = "/tmp/AsyncFw-test-rrd-legacy-i1-wrap.db";
  std::filesystem::remove(fn);

  DataArrayList db;
  db.resize(10);
  for (uint64_t p = 191; p <= 200; ++p) db[p % 10] = DataArray(std::to_string(p));
  writeLegacyRrdFile(fn, 200, db);

  Rrd rrd(10, 1, 0, fn);  // interval_ > 0 -> formula fallback
  EXPECT_MSG(rrd.lastIndex() == 200, "lastIndex=" + std::to_string(rrd.lastIndex()));
  EXPECT_MSG(rrd.count() == 10, "count=" + std::to_string(rrd.count()) +
                                    " (legacy file, no first_: interval_>0 fallback uses formula "
                                    "last_-dbSize+1=191 for the wrapped ring)");
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(0)) == "191", "readFromArray(0)=\"" + rrdItemStr(rrd.readFromArray(0)) + "\"");
  EXPECT_MSG(rrdItemStr(rrd.readFromArray(9)) == "200", "readFromArray(9)=\"" + rrdItemStr(rrd.readFromArray(9)) + "\"");

  std::filesystem::remove(fn);
}

// ============================================================================
// Group F. MainThread::exit(int) signal safety
// ============================================================================
//
// MainThread::exec() resets code_ to 0 on entry, and exit(int) uses CAS
// (0 -> code) so that:
//   - only the first successful write per exec() cycle changes code_,
//   - only the winning write wakes the loop via eventfd (failed CAS does
//     not reach eventfd_write),
//   - a new exec() cycle is isolated from the previous cycle's state.
//
// Covered paths:
//   1. exit(int) from a signal handler delivered on Main (raise).
//   2. exit(int) from a signal handler delivered on a worker (pthread_kill).
//      This is the cross-thread case where code_ is written by the worker
//      and read by Main — the std::atomic<int> requirement.
//   3. re-exec after a previous exit(int) picks up the new code, not the
//      stale one from the previous exec() cycle.
//   4. concurrent exit(int) spam from a worker while Main leaves exec().
//      Only the first spam write wins; further writes hit CAS-failure and
//      do NOT wake the loop again. Contract: exec() returns a positive
//      code, and the next exec() is not polluted by stale eventfd.
//
// Fundamental race window (not a bug, documented in MainThread.h):
//   A signal arriving AFTER the final code_.load() inside exec() is not
//   seen by that exec(). That's why test 4 asserts ret > 0, not an exact
//   value.
//
// Ordering constraints:
//   Group F MUST come after Group C (FileSystemWatcher) so the event loop
//   is clean when FileSystemWatcher schedules its 1500 ms timers. Group F
//   MAY leave code_ non-zero; any future exec() resets it.
//
// Behavioral note (documented in MainThread::exit):
//   exit(int) called BEFORE exec() with code != 0 is silently overwritten
//   by the reset in exec(). Call exit() from within the running loop
//   (invoke / Timer) if you need a non-zero code to survive.

#ifdef EXIT_ON_UNIX_SIGNAL

void exit_signal_handler(int signo) { MainThread::exit(100 + signo); }

bool install_exit_signal_handler(int signo) {
  struct sigaction sa {};
  sa.sa_handler = &exit_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  return ::sigaction(signo, &sa, nullptr) == 0;
}

// exit(int) called from the Main thread via a synchronous signal.
// raise() in a Timer callback delivers SIGUSR1 to Main; the handler runs
// inline on Main and calls exit(100 + SIGUSR1); exec() returns that code.
void test_exit_signal_from_main() {
  Timer::single(10, []() { ::raise(SIGUSR1); });
  int ret = MainThread::exec();
  EXPECT_MSG(ret == 100 + SIGUSR1, "exec() returned " + std::to_string(ret) + " (expected " + std::to_string(100 + SIGUSR1) + ")");
}

void test_exit_signal_from_worker() {
  Thread worker("SignalSender");

  // Subscribe BEFORE start(): start() only waits for setId(), and the
  // worker reaches startedEvent() (which emits started()) potentially
  // before start() returns. Subscribing after start() races with the
  // first (and only) emit — workerTid would stay 0 forever.
  std::atomic<std::uintptr_t> workerTid {0};
  FunctionConnectionGuard g = worker.started.connect([&workerTid]() { workerTid.store(reinterpret_cast<std::uintptr_t>(::pthread_self())); });

  worker.start();
  while (workerTid.load() == 0) std::this_thread::yield();

  Timer::single(10, [&workerTid]() { ::pthread_kill(reinterpret_cast<pthread_t>(workerTid.load()), SIGUSR2); });
  int ret = MainThread::exec();
  EXPECT_MSG(ret == 100 + SIGUSR2, "exec() returned " + std::to_string(ret) + " (expected " + std::to_string(100 + SIGUSR2) + ")");
}

// Re-exec with a specific code after a previous exec() cycle.
// Verifies that:
//   - exec() resets code_ to 0 on entry (Test 2 left it at 112),
//   - the new exit(7) CAS(0 -> 7) wins and its code is returned,
//   - no stale eventfd wakeup from a prior cycle short-circuits the poll.
void test_exit_signal_reexec_with_code() {
  Timer::single(10, []() { MainThread::exit(7); });
  int ret = MainThread::exec();
  EXPECT_MSG(ret == 7, "exec() returned " + std::to_string(ret) + " (expected 7)");
}

// Concurrent exit(int) spam from a worker while Main leaves exec().
//
// MainThread::exit(int) has two steps: CAS(0 -> K) on code_, then a wakeup.
// All racing callers execute the wakeup; only the first CAS winner fixes
// code_, so exec() returns the first writer's value. Extra wakeups coalesce
// in the eventfd counter — one eventfd_read drains the whole counter, so
// each exec() cycle runs at most one exit-task invocation even under heavy
// spam.
//
// Contract pinned here: exec() returns a positive code (the CAS winner)
// and does not crash under concurrent exit() calls. Exact write counts on
// eventfd are implementation-defined.
void test_exit_signal_spam() {
  std::atomic<bool> stop {false};
  std::atomic<int> lastWritten {0};

  Thread spam("ExitSpammer");
  spam.start();
  spam.invoke([&]() {
    for (int i = 1; !stop.load(); ++i) {
      MainThread::exit(i);
      lastWritten.store(i);
      std::this_thread::yield();
    }
  });

  int ret = MainThread::exec();
  stop = true;
  spam.invoke([]() {}, true);  // barrier: spam has left its loop

  EXPECT_MSG(ret > 0, "exec() returned non-positive code " + std::to_string(ret));
  lsNotice() << "test_exit_signal_spam: ret=" << ret << "lastWritten=" << lastWritten.load() << "(ret is the winning CAS value; lastWritten is the last spam iteration)";
}

#endif  // EXIT_ON_UNIX_SIGNAL

}  // namespace

// ============================================================================
// main
// ============================================================================

int main(int, char **) {
  lsNotice() << "set log output";
  LogStream::setCompleted(&log_output);

  // ThreadPool must live on a dedicated thread whose exec() is running for
  // the entire test run: pool workers use sync-invoke into the pool owner,
  // and coInvoke depends on it as the default offload target.
  Thread poolOwner("pool-owner");
  poolOwner.start();
  poolOwner.invoke([]() { Instance<ThreadPool>::create("MainTestsPool"); }, true);

  // --- Group A: ThreadPool --------------------------------------------
  test_threadpool_destroy_from_own_task();

  // --- Group B: Coroutine — basics ------------------------------------
  test_coroutine_basic_suspend_resume();
  test_coroutine_wait_blocks_until_done();
  test_coroutine_coInvoke_returns_value();
  test_coroutine_coInvoke_void();

  // --- Group D: Coroutine — TST ---------------------------------------
  // Runs before FileSystemWatcher: no MainThread::exec() involved, so it
  // is independent of the "Main loop must run last" constraint.
  test_coroutine_coInvoke_pool_lvalue_tst();
  test_coroutine_coInvoke_pool_xvalue_tst();
  test_coroutine_coInvoke_thread_lvalue_tst();
  test_coroutine_coInvoke_thread_xvalue_tst();
  test_coroutine_coInvoke_returns_tst();
  test_coroutine_coInvoke_lambda_capture_tst();

  // --- Group E: Rrd ---------------------------------------------------
  test_rrd_sequential_append();
  test_rrd_sequential_wrap();
  test_rrd_clear();
  test_rrd_append_explicit_index_auto_mode();
  test_rrd_append_auto_index_matches_rrdclient();
  test_rrd_append_explicit_index_with_interval();
  test_rrd_read_clamp_to_first();
  test_rrd_file_roundtrip_contiguous();
  test_rrd_file_roundtrip_sparse_time_based();
  test_rrd_readonly_load_contiguous();
  test_rrd_file_roundtrip_sparse_offset();
  test_rrd_file_roundtrip_first_equals_dbsize();
  test_rrd_first_persisted_sparse();
  test_rrd_reload_empty();
  test_rrd_reload_single_at_one();
  test_rrd_reload_single_at_high();
  test_rrd_first_persisted_unwrapped();
  test_rrd_interval_zero_sparse_start();
  test_rrd_legacy_file_interval0_wrapped();
  test_rrd_legacy_file_interval0_single_high();
  test_rrd_legacy_file_interval_positive_wrapped();

  // --- Group C: FileSystemWatcher -------------------------------------
  // Runs on MainThread::exec(). Must be last because MainThread::exit()
  // permanently finishes Main's event loop.
  test_filesystemwatcher_recreate_in_debounce_window();
  test_filesystemwatcher_scenarios();

  // --- Group F: MainThread::exit(int) signal safety -------------------
  // Runs AFTER Group C so FileSystemWatcher's timers/inotify events are
  // handled while the event loop is still pristine.
#ifdef EXIT_ON_UNIX_SIGNAL
  {
    bool ok = install_exit_signal_handler(SIGUSR1) && install_exit_signal_handler(SIGUSR2);
    if (ok) {
      test_exit_signal_from_main();
      test_exit_signal_from_worker();
      test_exit_signal_reexec_with_code();
      test_exit_signal_spam();
    } else {
      lsWarning() << "Group F (exit-signal): cannot install SIGUSR1/SIGUSR2, skipped";
    }
  }
#else
  lsNotice() << "Group F (exit-signal): skipped on this platform (no POSIX signals)";
#endif

  // --- Cleanup --------------------------------------------------------
  // Stop workers on their own thread so any pending sync-invokes from the
  // pool are served before we tear it down.
  poolOwner.invoke([]() {
    if (ThreadPool *pool = ThreadPool::instance()) pool->quit();
  }, true);

  // --- Report ---------------------------------------------------------
  if (g_failed.load()) {
    lsError() << LogStream::Color::Red << "main: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "main: " << g_total.load() << " checks passed";
  return 0;
}
