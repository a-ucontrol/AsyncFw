#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

#include <AsyncFw/Timer>

int main(int argc, char *argv[]) {
  AsyncFw::Thread::Waiter _w1;
  AsyncFw::Thread::Waiter _w2;

  AsyncFw::Timer::single(50, []() { AsyncFw::MainThread::exit(); });

  AsyncFw::Timer::single(1000, [&_w1, &_w2]() {
    _w1.complete();
    _w2.complete();
  });

  AsyncFw::Thread::current()->started.connect([&_w1, &_w2]() {
    AsyncFw::Thread::current()->invoke([&_w1]() {
      lsNotice() << "start wait 1";
      _w1.wait();
      lsNotice() << "end wait 1";
    });
    AsyncFw::Thread::current()->invoke([&_w2]() {
      lsNotice() << "start wait 2";
      _w2.wait();
      lsNotice() << "end wait 2";
    });
  });

  lsNotice() << "1 Start";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "1 End " << ret;

  AsyncFw::Timer::single(50, []() { AsyncFw::MainThread::exit(); });

  AsyncFw::Timer::single(1000, [&_w1, &_w2]() {
    _w1.complete();
    _w2.complete();
  });

  lsNotice() << "2 Start";
  ret = AsyncFw::MainThread::exec();
  lsNotice() << "2 End " << ret;

  return ret;
}
