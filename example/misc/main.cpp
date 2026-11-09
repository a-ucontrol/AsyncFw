#include <AsyncFw/LogStream>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Instance>

struct PoolHolder {
  AsyncFw::ThreadPool *pool = nullptr;
  PoolHolder() { pool = new AsyncFw::ThreadPool("pool"); }
  ~PoolHolder() {
    lsInfo();
    pool->quit();
    delete pool;
  }
  static inline AsyncFw::Instance<PoolHolder> instance_;
};

#include <AsyncFw/MainThread>

int main(int, char **) {
  AsyncFw::Instance<PoolHolder>::create();

  AsyncFw::Thread::current()->invoke([]() { AsyncFw::MainThread::exit(); });

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application" << ret;
  return 0;
}
