/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <memory>
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

class Sender {
public:
  Sender() {
    timer.timeout.connect([this]() {
      lsDebug() << cnt << "send from thread:" << AsyncFw::AbstractThread::current();
      connector(++cnt);
      if (cnt == 3) {
        timer.stop();
        AsyncFw::MainThread::quit();
      }
    });
    timer.start(10);
  }

  AsyncFw::FunctionConnector<int>::Protected<Sender> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  void receive(int val) { lsDebug() << val << "receive in thread:" << AsyncFw::AbstractThread::current(); }
};

int main(int argc, char *argv[]) {
  AsyncFw::Thread thread("SenderThread");
  std::shared_ptr<Sender> sender;  // after thread

  thread.start();
  thread.invoke([&sender]() {
    sender = std::make_shared<Sender>();  // created in thread SenderThread
  }, true);
  Receiver receiver;

  sender->connector.connect(&Receiver::receive, &receiver);

  auto lambda = [](int val) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    lsNotice() << "sender->connector (lambda lvalue)" << val;
  };

  sender->connector.connect([](int val) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    lsNotice() << "sender->connector (lambda rvalue)" << val;
  });

  sender->connector.connect(lambda);

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  logNotice() << "End Application";

  return ret;
}
//! [snippet]
