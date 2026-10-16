// Tests for classes under main/.
// Groups:
//   A. ThreadPool          — destroy thread from its own task.
//   B. Coroutine           — suspend/resume, wait, coInvoke.
//   C. FileSystemWatcher   — shared dir watch, file lifecycle, delete+recreate.
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
#include <AsyncFw/LogStream>

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

// ============================================================================
// Group A. ThreadPool
// ============================================================================

// Thread::destroy() must be safe to call from within a task running on that
// same thread. Internally destroy() sync-invokes back to the pool's owning
// thread, so the pool must live on a separate AsyncFw::Thread whose exec()
// is running while the main thread waits here.
void test_threadpool_destroy_from_own_task() {
  ThreadPool *pool = ThreadPool::instance();
  EXPECT(pool != nullptr);

  std::atomic<bool> reachedEnd{false};

  AbstractThreadPool::Thread *t = pool->createThread("DestroyFromOwnTask");
  t->start();
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
// Group B. Coroutine
// ============================================================================

// Basic co_await with synchronous resume: control returns to the caller
// frame and the coroutine body completes without any external event source.
void test_coroutine_basic_suspend_resume() {
  std::atomic<int> steps{0};

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

  std::atomic<bool> completed{false};
  std::atomic<bool> waitReturned{false};

  t.invoke([&]() {
    auto coro = [&]() -> CoroutineTask {
      co_await CoroutineAwait<void>([](CoroutineHandle h) {
        Timer::single(20, [h]() { h.resume(); });
      });
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

  std::atomic<int> result{0};
  std::atomic<bool> finished{false};

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

  std::atomic<bool> ranCallable{false};
  std::atomic<bool> finished{false};

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
// Group C. FileSystemWatcher
// ============================================================================

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
  std::atomic<bool> scenarioAOk{false};
  std::atomic<int> scenarioBNotify{0};
  std::atomic<int> scenarioCRemoved{0};
  std::atomic<int> scenarioCCreated{0};

  FunctionConnectionGuard g = watcher.notify.connect([&](const std::string &name, int event) {
    lsInfo() << "notify:" << name << event;
    // event: -1 = Removed, 0 = Changed, 1 = Created.
    if (phase == 0 && name == f2 && event == 1) {
      scenarioAOk = true;
    }
    if (phase == 1 && name == f3 && event == 0) {
      ++scenarioBNotify;
    }
    if (name == f4 && event == -1) ++scenarioCRemoved;
    if (name == f4 && event == 1) ++scenarioCCreated;
  });

  // ---- Scenario A -----------------------------------------------------
  watcher.addPath(f1);
  watcher.addPath(f2);
  watcher.removePath(f1);
  {
    File _f{f2};
    _f.open(std::ios::binary | std::ios::out);
  }

  // ---- Scenario B (deferred) ------------------------------------------
  Timer::single(300, [&, f3]() {
    phase = 1;
    {
      File _f{f3};
      _f.open(std::ios::binary | std::ios::out);
    }
    watcher.addPath(f3);
    watcher.removePath(f3);
    watcher.addPath(f3);
    {
      File _f{f3};
      _f.open(std::ios::binary | std::ios::out | std::ios::app);
      _f.write("b", 1);
    }
  });

  // ---- Scenario C (delete + recreate) ---------------------------------
  {
    File _f{f4};
    _f.open(std::ios::binary | std::ios::out);
  }
  watcher.addPath(f4);
  std::filesystem::remove(f4);
  Timer::single(300, [f4]() {
    File _f{f4};
    _f.open(std::ios::binary | std::ios::out);
  });

  // ---- Final report ---------------------------------------------------
  Timer::single(1500, [&]() {
    lsNotice() << "Scenario A:" << (scenarioAOk.load() ? "OK" : "BUG (Created(f2) not received)");
    lsNotice() << "Scenario B: Changed(f3) count =" << scenarioBNotify.load();
    lsNotice() << "Scenario C: Removed(f4) =" << scenarioCRemoved.load()
               << "Created(f4) =" << scenarioCCreated.load();

    EXPECT(scenarioAOk.load());
    EXPECT(scenarioBNotify.load() == 1);
    EXPECT(scenarioCRemoved.load() >= 1);
    EXPECT(scenarioCCreated.load() >= 1);

    MainThread::exit();
  });

  MainThread::exec();
}

}  // namespace

int main(int, char **) {
  // ThreadPool must live on a dedicated thread whose exec() is running for
  // the entire test run: pool workers use sync-invoke into the pool owner,
  // and coInvoke depends on it as the default offload target.
  Thread poolOwner("pool-owner");
  poolOwner.start();
  poolOwner.invoke([]() { Instance<ThreadPool>::create("MainTestsPool"); }, true);

  // --- Group A: ThreadPool --------------------------------------------
  test_threadpool_destroy_from_own_task();

  // --- Group B: Coroutine ---------------------------------------------
  test_coroutine_basic_suspend_resume();
  test_coroutine_wait_blocks_until_done();
  test_coroutine_coInvoke_returns_value();
  test_coroutine_coInvoke_void();

  // --- Group C: FileSystemWatcher -------------------------------------
  // Runs on MainThread::exec(). Must be last because MainThread::exit()
  // permanently finishes Main's event loop.
  test_filesystemwatcher_scenarios();

  // --- Cleanup --------------------------------------------------------
  // Stop workers on their own thread so any pending sync-invokes from the
  // pool are served before we tear it down.
  poolOwner.invoke([]() {
    if (ThreadPool *pool = ThreadPool::instance()) pool->quit();
  }, true);

  // --- Report ---------------------------------------------------------
  if (g_failed.load()) {
    lsError() << LogStream::Color::Red
              << "main: " << g_failed.load() << '/' << g_total.load() << " checks failed";
    return 1;
  }
  lsNotice() << "main: " << g_total.load() << " checks passed";
  return 0;
}
