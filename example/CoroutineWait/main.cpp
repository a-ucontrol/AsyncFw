#include <ThreadPool.h>
#include <Coroutine.h>
#include <MainThread.h>
#include <Timer.h>
#include <Log.h>

#define CURRENT_TIME_MS (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())

using namespace AsyncFw;

CoroutineTask task(uint64_t &end, int _ms) {
  logDebug() << "task started";
  co_await CoroutineAwait([&end, &_ms](CoroutineHandle _h) {
    ThreadPool::async(
        [&_ms]() {
          logDebug() << "async" << _ms << Thread::currentThread()->name();
          std::this_thread::sleep_for(std::chrono::milliseconds(_ms));
          logDebug() << "async end" << _ms << Thread::currentThread()->name();
        },
        [&end, _h]() {
          logDebug() << "resume";
          end = CURRENT_TIME_MS;
          _h.resume();
          logDebug() << "resume end";
        });
  });
  logDebug() << "task finished";
}

void tst(int _ms) {
  uint64_t tst_time = CURRENT_TIME_MS;
  logNotice() << _ms << "start";
  uint64_t ms_start = CURRENT_TIME_MS;
  logDebug() << _ms << "START" << ms_start;
  uint64_t ms_end;
  CoroutineTask _ct = task(ms_end, _ms);
  _ct.wait();
  logNotice() << _ms << ms_end - ms_start << "=====" << CURRENT_TIME_MS - tst_time;
}

int main(int argc, char *argv[]) {
  LogMinimal log;
  ThreadPool _pool(5);

  //tst(5000);

  Timer::single(10, []() { tst(100); });
  Timer::single(10, []() { tst(1500); });
  Timer::single(10, []() { tst(2000); });
  Timer::single(10, []() { tst(500); });
  Timer::single(10, []() { tst(1000); });

  Timer::single(5000, []() {
    logNotice() << "MainThread::instance()->quit()";
    MainThread::exit();
  });

  logNotice() << "MainThread::instance()->exec()";

  return MainThread::exec();
}
