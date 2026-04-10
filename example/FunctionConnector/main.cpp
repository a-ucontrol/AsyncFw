/*
Copyright (c) 2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/Log>

class MethodConnectionExample {
public:
  void method(int val) { lsNotice() << val; }
};

class Sender {
public:
  Sender() {
    timer.timeout([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << cnt << "send from thread:" << ct->name() << ct->id();
      connector(cnt++);
      if (cnt == 3) AsyncFw::MainThread::exit(0);
    });
    timer.start(10);
  }

  mutable AsyncFw::FunctionConnectorProtected<Sender>::Connector<int> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  Receiver(const std::string &name, const Sender &sender) {
    fcg = sender.connector([name_ = name](int val) {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::currentThread();
      logInfo() << name_ << "received" << val << "run in thread" << ct->name() << ct->id();
    });
  }
  AsyncFw::FunctionConnectionGuard fcg;
};

int main(int argc, char *argv[]) {
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

  MethodConnectionExample tst;
  sender->connector.connect(&tst, &MethodConnectionExample::method);
  sender->connector([](int val) { lsNotice() << "sender->connector (lambda)" << val; });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  delete sender;
  return ret;
}
//! [snippet]
