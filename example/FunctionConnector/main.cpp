#include <AsyncFw/core/Thread.h>
#include <AsyncFw/core/FunctionConnector.h>
#include <AsyncFw/Timer.h>
#include <AsyncFw/MainThread.h>
#include <AsyncFw/Log.h>

class Sender {
public:
  Sender() {
    timer.timeout([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << cnt << "send from thread:" << ct->name() << ct->id();
      connector(cnt++);
      if (cnt == 3) AsyncFw::MainThread::instance()->exit(0);
    });
    timer.start(1000);
  }

  mutable AsyncFw::FunctionConnectorProtected<Sender>::Connector<int> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  Receiver(const std::string &name, const Sender &sender) {
    sender.connector([name_ = name](int val) {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << name_ << "received" << val << "run in thread" << ct->name() << ct->id();
    });
  }
};

int main(int argc, char *argv[]) {
  AsyncFw::LogMinimal log;

  Sender *sender;

  AsyncFw::Thread thread("SenderThread");
  thread.start();
  thread.invokeMethod(
      [&sender]() {
        sender = new Sender;  // created in thread SenderThread
      },
      true);

  Receiver receiver1("R1", *sender);
  Receiver receiver2("R2", *sender);

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::instance()->exec();

  logNotice() << "End Applicaiton";
  delete sender;
  return ret;
}
