# AsyncFw

Async Framework is a lightweight C++20 runtime for asynchronous programming. It provides timers, poll notifiers, thread-safe signals/slots, asynchronous sockets, and native C++20 coroutines on top of a custom event loop, so long-running work can be offloaded to worker threads without blocking the main loop.

See the [GitHub repository], [documentation], and [examples] for details.

[GitHub repository]: https://github.com/a-ucontrol/AsyncFw
[documentation]: https://a-ucontrol.github.io/AsyncFw
[examples]: https://a-ucontrol.github.io/AsyncFw/examples.html

---

## Core Architecture & Components

### 1. The Core Backbone: `AbstractThread`

`AbstractThread` is the base of the framework. It is a `std::thread` wrapper with its own **task queue** and **event loop**.

* **Reactor loop**: `AbstractThread::exec()` runs a polling loop. The backend is selected at build time:
  * **`io_uring`** when `ENABLE_IO_URING` is defined (`io_uring_wait_cqe_timeout`),
  * **`epoll`** on Linux (`epoll_wait`, optionally edge-triggered via `EPOLL_EDGE_TRIGGERED`),
  * **`poll()`** elsewhere (including `WSAPoll` on Windows).
* **Inter-thread wakeup**: when another thread posts a task via `invoke()`, the task is added to a synchronized queue and the target thread's blocking wait is interrupted. The wake mechanism is build-time selectable: `eventfd` on Linux, `pipe2` on generic POSIX, a connected UDP `socketpair` on Windows, or a self-close socket in the debug configuration.
* **Nested exec()**: `exec()` may be re-entered. The framework tracks the nesting level, drains pending tasks first, and yields to timers/poll events instead of blocking.
* **Graceful shutdown**: `requestInterrupt()` and `waitInterrupted()` move the thread through a defined state sequence (`WaitStarted → Running → WaitInterrupted → Interrupted → WaitFinished → Finished`), ensuring queued work is flushed before resources are released.

### 2. Cross-Thread Signals: `FunctionConnector`

`FunctionConnector` is a thread-safe signal/slot mechanism. The `Protected` variant restricts signal emission to the owning class, while any external component can connect using a `const` reference.

Connection policies are chosen per subscriber:

| Policy | Behaviour |
|---|---|
| `Direct` | Runs in the emitting thread. |
| `Queued` | Posts a task to the receiver's thread event loop. |
| `Sync` | Posts to the receiver's thread and blocks the emitter until done. |
| `Auto` (default) | `Direct` if emitter and receiver share a thread, `Queued` otherwise. |

```cpp
#include <string>
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Thread>
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
      connector(++cnt, "");
      if (cnt == 10) {
        timer.stop();
        AsyncFw::MainThread::exit(0);
      }
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

int main(int, char **) {
  AsyncFw::Thread thread("SenderThread");
  thread.start();

  Sender *sender = nullptr;
  thread.invoke([&sender]() { sender = new Sender(); }, true);
  if (!sender) {
    lsError() << "Sender creation failed";
    return 1;
  }

  Receiver receiver1("R1", *sender);
  Receiver receiver2("R2", *sender);

  MethodConnectionExample _e;
  sender->connector.connect(&MethodConnectionExample::method, &_e);

  auto lambda = [](int val, const std::string &) {
    lsNotice() << "sender->connector (lambda)" << val;
  };
  sender->connector.connect(lambda);

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();

  // Destroy the Sender on the thread that owns it.
  thread.invoke([&sender]() { delete sender; sender = nullptr; }, true);
  return ret;
}
```

### 3. Coroutines and RPC-Style Dispatch: `coInvoke`

The coroutine layer (`CoroutineTask`, `CoroutineAwait`, `CoroutineInvokeAwait`) ties C++20 coroutines into the cross-thread infrastructure.

* **Thread marshalling**: when a coroutine yields via `co_await`, work is dispatched to a target thread or thread pool. When it finishes, `promise_type::resume_queued()` schedules the continuation back onto the thread where the coroutine was created.
* **State transport**: the `promise_type` inherits from `AnyData`, so a background task can store its result in the coroutine frame and `await_resume` can retrieve it.
* **`coInvoke`**: awaitable helper for calling free functions, lambdas, or member methods and getting the return value back. Without an explicit target thread the call is scheduled onto the global `ThreadPool` instance, which must have been created during bootstrap.

```cpp
#include <AsyncFw/MainThread>
#include <AsyncFw/ThreadPool>
#include <AsyncFw/Coroutine>
#include <AsyncFw/LogStream>

int heavyCalculation(int input) {
  return input * 42;
}

AsyncFw::CoroutineTask runApplicationLogic() {
  lsNotice() << "Starting application flow on Main thread...";
  int computationResult = co_await AsyncFw::coInvoke(heavyCalculation, 10);
  lsNotice() << "Computation returned:" << computationResult;
  AsyncFw::MainThread::exit(0);
}

int main(int, char **) {
  // coInvoke() without an explicit target thread uses the global ThreadPool,
  // which must exist before the first co_await.
  AsyncFw::Instance<AsyncFw::ThreadPool>::create("CoroutineExamplePool");

  runApplicationLogic();  // fire-and-forget; MainThread::exit() ends exec()
  logNotice() << "Booting Main Engine Event Loop...";
  return AsyncFw::MainThread::exec();
}
```

Waiting on a timer:

```cpp
#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

AsyncFw::CoroutineTask task(AsyncFw::Timer *timer) {
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  co_await timer->coTimeout(10);
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  co_await timer->coTimeout(10);
  lsDebug() << "coro task" << std::chrono::system_clock::now();
  AsyncFw::MainThread::exit(0);
}

int main(int, char **) {
  AsyncFw::Timer timer1;
  timer1.start(10);
  AsyncFw::Timer timer2;

  timer1.timeout.connect([]() { lsDebug() << std::chrono::system_clock::now() << " timer1 timeout"; });
  timer2.timeout.connect([]() { lsDebug() << std::chrono::system_clock::now() << " timer2 timeout"; });

  task(&timer2);

  logNotice() << "Start Application";
  int ret = AsyncFw::MainThread::exec();
  return ret;
}
```

### 4. Network Worker Thread: `Thread` and Sockets

`Thread` derives from `AbstractThread` and specialises in managing network connections (`AbstractSocket`, `AbstractTlsSocket`, `DataArraySocket`).

* **Socket ownership**: `Thread` owns the sockets registered on it. On destruction it stops its loop, waits for it to finish, and closes/deletes every remaining socket.
* **`SIGPIPE` blocking**: on POSIX systems, `Thread::startedEvent()` blocks `SIGPIPE` via `pthread_sigmask` so a remote disconnect during a TLS handshake does not terminate the process.
* **TLS layer**: `AbstractTlsSocket` overrides read/write paths to `SSL_read`/`SSL_write` and drives the non-blocking handshake state machine (`SSL_accept`/`SSL_connect`).

### 5. TLS Configuration: `TlsContext`

`TlsContext` wraps `SSL_CTX` and separates TLS configuration from network I/O.

* **Reference-counted lifetime**: the underlying OpenSSL context is shared between `TlsContext` copies and released once the last copy is destroyed.
* **PKI helpers**: `generateKey` (currently RSA via `EVP_RSA_gen`), `generateRequest` (CSR), and `signRequest` (x509 signing). `appendTrusted`/`setDefaultVerifyPaths` configure the trust store.
* **Peer verification**: `setVerifyPeer`, `setVerifyName` (hostname / SAN check), and `setIgnoreErrors` (`IgnoreErrors::TimeValidity`) control validation during the handshake.
* **Immutability**: the class is documented as not thread-safe for modification. Configure it fully before assigning it to a socket.

### 6. Task Distribution: `ThreadPool`

`ThreadPool` manages a set of reusable worker threads.

* **Load balancing**: `getThread()` returns the least-loaded worker, creating new workers lazily up to `workThreadsSize`.
* **Callback round-trip**: `ThreadPool::async(thread, fn, result)` runs `fn` on the worker and invokes `result` back on the caller's thread with the return value (or with no argument for `void` returns).

### 7. Logging: `LogStream`

`LogStream` is a thread-safe diagnostic stream with RAII lifetime.

* **RAII flush**: a temporary `LogStream` instance accumulates output and flushes when the statement ends. Concurrent output is serialised via `std::osyncstream`.
* **Two interfaces**: C++ stream insertion (`operator<<`) and `std::format`-style `output("value: {}", v)`.
* **Level macros**: `lsTrace`/`lsDebug`/... macros can be compiled out via `LS_NO_*` defines; `logTrace`/`logDebug`/... are always compiled in.

### 8. Application Master: `MainThread`

`MainThread` derives from `Thread` and drives the primary application thread.

* **`MainThread::exec()`**: starts the event loop on the current thread. `MainThread::exit()` and `MainThread::exit(code)` request shutdown from any thread (and, when `EXIT_ON_UNIX_SIGNAL` is active, from a POSIX signal handler via a self-pipe / `eventfd`).
* **Qt integration (`USE_QAPPLICATION`)**: `MainThread::exec()` delegates to `qApp->exec()`. Framework primitives are mapped to Qt equivalents — timers via `QObject::startTimer`, queued tasks via `QMetaObject::invokeMethod`, and descriptor readiness via `QSocketNotifier`.

### 9. mDNS / DNS-SD: `MulticastDns`

`MulticastDns` implements mDNS and DNS-SD over the framework's event loop.

* **Event-loop integration**: descriptor readiness for mDNS sockets is registered with the owning `AbstractThread`, so no extra thread is required.
* **Multiple instances**: each `MulticastDns` instance owns its own sockets and buffers, so different instances can browse or publish independent service types concurrently.
* **Thread safety**: the internal host list is guarded by a `std::mutex`; sockets and buffers are released on destruction.
* **Signals**: `hostAdded`, `hostChanged`, and `hostRemoved` are `FunctionConnector`s. A goodbye packet (TTL=0) is reported through `hostChanged` with `port == 0`; removal (expiry or explicit deletion) is reported through `hostRemoved`.
