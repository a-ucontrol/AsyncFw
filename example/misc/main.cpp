#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>
#include <AsyncFw/DataArray>

int main(int, char **) {
  AsyncFw::Thread::current()->invoke([]() { AsyncFw::MainThread::exit(); });

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application " << ret;
  return ret;
}
