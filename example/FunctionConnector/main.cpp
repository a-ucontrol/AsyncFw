/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

class MethodConnectionExample {
public:
  void method(int val, const std::string &str) { lsNotice() << val << str; }
};

class Sender {
public:
  Sender() {
    timer.timeout.connect([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      logInfo() << cnt << "send from thread:" << ct->name() << ct->id();
      connector(cnt++, "");
      if (cnt == 1) AsyncFw::MainThread::exit(0);
    });
    timer.start(10);
  }

  AsyncFw::FunctionConnector<int, const std::string &>::Protected<Sender> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  Receiver(const std::string &name, const Sender &sender) {
    fcg = sender.connector.connect([name_ = name](int val, const std::string &) {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      logInfo() << name_ << "received" << val << "run in thread" << ct->name() << ct->id();
    });
  }
  AsyncFw::FunctionConnectionGuard fcg;
};

int main(int argc, char *argv[]) {
  Sender *sender;

  AsyncFw::Thread thread("SenderThread");
  thread.start();
  thread.invoke([&sender]() {
    sender = new Sender;  // created in thread SenderThread
  }, true);

  Receiver receiver1("R1", *sender);
  Receiver receiver2("R2", *sender);

  MethodConnectionExample _e;
  sender->connector.connect(&MethodConnectionExample::method, &_e);

  auto lambda = [](int val, const std::string &) { lsNotice() << "sender->connector (lambda)" << val; };
  sender->connector.connect(lambda);

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  delete sender;
  return ret;
}
//! [snippet]
