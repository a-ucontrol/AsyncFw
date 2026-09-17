#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

int main(int argc, char *argv[]) {
  AsyncFw::Thread::current()->invoke([]() { AsyncFw::MainThread::exit(); });

  lsNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Application " << ret;
  return ret;
}
