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

struct TST {
  TST(int i) : val(i) { lsDebug() << val; }
  ~TST() { lsDebug() << val; }
  TST(const TST &v) {
    val = v.val;
    lsDebug() << "copy" << val;
  }
  TST(TST &&v) {
    val = v.val;
    v.val = -1;
    lsDebug() << "move" << val;
  }
  int val = 0;
};

class Sender {
public:
  Sender() {
    timer.timeout.connect([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      lsDebug() << cnt << "send from thread:" << ct->name() << ct->id();
      connector(cnt++, _tst);
      if (cnt == 1) AsyncFw::MainThread::exit(0);
    });
    timer.start(10, true);
  }

  AsyncFw::FunctionConnector<int, TST>::Protected<Sender> connector;

private:
  TST _tst {1010};
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  void send(int _i, TST _t) { connector(_i, _t); }
  AsyncFw::FunctionConnector<int, TST>::Protected<Receiver> connector;
};

int main(int argc, char *argv[]) {
  Sender *sender;

  AsyncFw::Thread thread("SenderThread");
  thread.start();
  thread.invoke([&sender]() {
    sender = new Sender;  // created in thread SenderThread
  }, true);
  Receiver receiver;
  AsyncFw::FunctionConnectionGuardList _gl;
  _gl += sender->connector.connect<AsyncFw::AbstractFunctionConnector::Connection::Queued>(&Receiver::send, &receiver);
  _gl += receiver.connector.connect([](int _i, TST _t) { lsInfo() << "receiver" << _i << _t.val; });

  TST captured {999};
  auto lambda = [captured](int val, TST tst) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    lsNotice() << "sender->connector (lambda lvalue)" << val << tst.val << captured.val;
  };

  TST captured_rv {777};
  sender->connector.connect<AsyncFw::AbstractFunctionConnector::Connection::Queued>([captured_rv](int val, TST tst) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    lsNotice() << "sender->connector (lambda rvalue)" << val << tst.val << captured_rv.val;
  });

  sender->connector.connect<AsyncFw::AbstractFunctionConnector::Connection::Queued>(lambda);
  sender->connector.connect<AsyncFw::AbstractFunctionConnector::Connection::Queued>(lambda);

  logNotice() << "Start Application";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Application";
  delete sender;
  return ret;
}
//! [snippet]
