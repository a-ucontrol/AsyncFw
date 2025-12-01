#include <AsyncFw/MainThread.h>
#include <AsyncFw/core/TlsSocket.h>
#include <AsyncFw/Log.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  static TcpSocket *create() { return new TcpSocket; }
  void stateEvent() { logDebug() << "State event:" << static_cast<int>(state()); }
  void readEvent() { received(read()); }
  AsyncFw::FunctionConnectorProtected<TcpSocket>::Connector<const AsyncFw::DataArray &> received;
};

int main(int argc, char *argv[]) {
  AsyncFw::LogMinimal log;

  AsyncFw::ListenSocket ls;
  ls.setIncomingConnection([](int fd, const std::string &address) {
    TcpSocket *socket = TcpSocket::create();
    socket->setDescriptor(fd);
    socket->received([socket](const AsyncFw::DataArray &data) {
      logNotice() << "received:" << data;
      socket->write("Answer\n");
      AsyncFw::MainThread::instance()->exit(0);
    });
    logInfo() << "Incoming:" << fd << address;
    return true;
  });

  ls.listen("0.0.0.0", 18080);

  logNotice() << "Start Applicaiton";
  int ret = AsyncFw::MainThread::instance()->exec();
  logNotice() << "End Applicaiton";

  return ret;
}
