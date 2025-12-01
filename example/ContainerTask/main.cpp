#include <queue>
#include <MainThread.h>
#include <Timer.h>
#include <Task.h>
#include <core/LogStream.h>

int main(int argc, char *argv[]) {
  AsyncFw::Thread thread;
  AsyncFw::Timer timer;
  std::queue<std::shared_ptr<AsyncFw::AbstractTask>> tasks;

  for (int i = 0; i != 10; ++i) {
    std::shared_ptr<AsyncFw::AbstractTask> task = std::shared_ptr<AsyncFw::AbstractTask>(new AsyncFw::Task([](std::any *data) { logInfo() << "task:" << std::any_cast<int>(*data); }, &thread));
    task->setData(i);
    tasks.push(task);
  }

  timer.timeout([&tasks]() {
    if (tasks.empty()) {
      AsyncFw::MainThread::instance()->exit(0);
      return;
    }
    lsInfoGreen() << tasks.front()->running();
    tasks.front()->invoke();
    lsInfoGreen() << tasks.front()->running();
    tasks.pop();
  });

  thread.start();
  timer.start(500);

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::instance()->exec();
  logNotice() << "End Applicaiton";
  return ret;
}
