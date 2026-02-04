#include <core/Thread.h>
#include <MainThread.h>
#include <core/TlsSocket.h>
#include <core/TlsContext.h>
#include <AddressInfo.h>
#include <Log.h>

class TcpSocket : public AsyncFw::AbstractTlsSocket {
public:
  void stateEvent() {
    logDebug() << "State event:" << static_cast<int>(state_);
    if (state_ == Active) {
      logDebug() << "Send request";
      write("GET /a-ucontrol/AsyncFw HTTP/1.1\r\nHost:github.com\r\nConnection:close\r\n\r\n");
    } else if (error() >= Closed) {
      if (error() != Closed) logError() << errorString();
      else {
        logNotice() << "Received:" << da;
        logDebug() << da.view(0, 512) << "...";
      }
      AsyncFw::MainThread::exit(0);
    }
  }
  void readEvent() {
    AsyncFw::DataArray _da = read();
    logTrace() << "Read event:" << _da.size() << std::endl << _da.view(0, 256);
    da += _da;
  }

private:
  AsyncFw::DataArray da;
};

int main(int argc, char *argv[]) {
  AsyncFw::LogMinimal log;

  AsyncFw::TlsContext context;

  if (!context.setDefaultVerifyPaths()) {
    logError("Can't set default verify paths");
    return -1;
  }
  context.setVerifyName("github.com");
  TcpSocket socket;
  socket.setContext(context);

  AsyncFw::AddressInfo addressInfo;
  addressInfo.resolve("github.com");
  addressInfo.completed([&socket](int r, const std::vector<std::string> &list) {
    if (r == 0 && !list.empty()) {
      for (const std::string _s : list) logNotice() << _s;
      socket.connect(list[0], 443);
    }
  });

  logNotice() << "Start Applicaiton";

  int ret = AsyncFw::MainThread::exec();

  logNotice() << "End Applicaiton";
  return ret;
}
