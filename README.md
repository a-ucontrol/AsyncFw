# AsyncFw

Async Framework is a lightweight, high-performance C++20 runtime that brings asynchronous programming to your applications. It features built-in support for timers, poll notifiers (event-driven I/O), thread-safe signals/slots, sockets, and native C++20 coroutines.

AsyncFw allows you to run long-running operations concurrently across multiple threads without ever blocking your primary event loop.

See the [GitHub repository], [documentation] and [examples] for details.

[GitHub repository]: https://github.com/a-ucontrol/AsyncFw
[documentation]: https://a-ucontrol.github.io/AsyncFw
[examples]: https://a-ucontrol.github.io/AsyncFw/examples.html

---

## Core Architecture & Components

### 1. The Core Backbone: `AbstractThread`
`AbstractThread` is the foundational element of the entire framework. It goes beyond a simple `std::thread` wrapper by maintaining its own **Task Queue** and a localized **Event Loop (Reactor)**.
* **The Reactor Loop**: Internally, `AbstractThread::exec()` runs a highly optimized polling loop. On Linux, it leverages **`epoll`** (`epoll_wait`); on other systems, it falls back to standard **`poll`**. 
* **Non-Blocking Inter-Thread Wakeup**: When a foreign thread posts a task via `invoke()`, it inserts the task into a synchronized queue and wakes up the target thread's blocking `epoll_wait` instantly. On Linux, this is achieved via **`eventfd`**, which introduces near-zero overhead compared to traditional pipe-based signaling.
* **Intelligent Nested Loops**: It handles nested invocation processing safely (`nested_` tracking). If an inner loop is executed, it can yield to process waiting asynchronous callbacks, prevent deadlines from slipping, and protect against stack overflow or event deadlocks.
* **Deterministic Graceful Shutdown**: Using `requestInterrupt()` and `waitInterrupted()`, a thread transitions through safe structural states, ensuring that pending items in the pipeline are safely flushed before resources are released.

### 2. Cross-Thread Signals: `FunctionConnector`
`FunctionConnector` provides a synchronized signaling system. By utilizing the `Protected` variant, you enforce strict encapsulation: **only the owner class can emit the signal**, while any external component can safely connect to it using a clean, non-mutating `const` reference.

* **Code Example (Signals & Slots):**
  ```cpp
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
        connector(cnt++, "");
        if (cnt == 10) AsyncFw::MainThread::exit(0);
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
    thread.invoke([&sender]() { sender = new Sender; }, true);

    Receiver receiver1("R1", *sender);
    Receiver receiver2("R2", *sender);

    MethodConnectionExample _e;
    sender->connector.connect(&MethodConnectionExample::method, &_e);

    auto lambda = [](int val, const std::string &) { lsNotice() << "sender->connector (lambda)" << val; };
    sender->connector.connect(lambda);

    logNotice() << "Start Application";
    int ret = AsyncFw::MainThread::exec();
    delete sender;
    return ret;
  }
  ```
### 3. Deep Coroutine Orchestration & RPC: `coInvoke`
`AsyncFw` supercharges C++20 coroutines by tying their suspension hooks directly into the framework's cross-thread architecture via `CoroutineTask` and generic awaiters (`CoroutineAwait`, `CoroutineInvokeAwait`).
* **Automated Thread Marshalling**: When a coroutine yields via `co_await`, heavy jobs are offloaded to background workers. Once done, the internal `promise_type::resume_queued()` automatically schedules continuation back onto the thread where the coroutine was originally created.
* **Stateful Data Transport**: The coroutine `promise_type` inherits from `AnyData`, turning the underlying compiler-generated coroutine frame into a dynamic data store. Background tasks push computed results directly into the frame.
* **The `coInvoke` Engine**: Provides an elite, non-blocking remote procedure call (RPC) syntax. Developers can await free functions, lambdas, or class member methods using `co_await coInvoke(...)`.
  
* **Code Example (Offloading work to ThreadPool):**
    ```cpp
    #include <chrono>
    #include <string>
    #include <AsyncFw/MainThread>
    #include <AsyncFw/Coroutine>
    #include <AsyncFw/LogStream>
  
    int heavyCalculation(int input) {
      return input * 42;
    }
  
    AsyncFw::CoroutineTask runApplicationLogic() {
      lsNotice() << "Starting application flow on Main Thread...";
      int computationResult = co_await AsyncFw::coInvoke(heavyCalculation, 10);
      lsNotice() << "Computation successfully returned: " << computationResult;
      AsyncFw::MainThread::exit(0);
    }
  
    int main(int argc, char *argv[]) {
      runApplicationLogic();
      logNotice() << "Booting Main Engine Event Loop...";
      int ret = AsyncFw::MainThread::exec();
      return ret;
    }
    ```
  
* **Code Example (Asynchronous wait via Timer):**
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
  
    int main(int argc, char *argv[]) {
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
  
### 4. Network Worker Thread: `Thread` & Secure Sockets
  `Thread` inherits from `AbstractThread` and specializes in managing network connections, asynchronous sockets (`AbstractSocket`), and connection pools.
  * **Socket Lifecycle Management**: Acts as an RAII container for asynchronous sockets. When a `Thread` instance is destroyed, it automatically interrupts its loop, safely waits for finish, and gracefully closes and deletes all registered sockets.
  * **Robust Crash Prevention (`SIGPIPE`)**: On POSIX/Unix systems, the worker automatically blocks `SIGPIPE` at startup. This prevents the entire application from terminating abruptly if a remote client disconnects during a TLS handshake.
  * **TLS/SSL Cryptographic Layer**: Through `AbstractTlsSocket`, the framework builds a secure layer on top of standard network sockets.
  
### 5. Cryptographic State & PKI: `TlsContext`
  `TlsContext` acts as the secure state machine wrapping OpenSSL (`SSL_CTX`), decoupling TLS configuration logic from network I/O routines.
  * **Automated OpenSSL Lifecycle**: Uses a reference-counted `Private` implementation to manage raw OpenSSL pointers, guaranteeing that resources like `SSL_CTX_free` are safely called.
  * **On-the-Fly PKI Generation**: Natively provides capability to generate RSA keys (`generateKey`), export certificate signing requests (`generateRequest`), and sign x509 certificates (`signRequest`).
  
### 6. Smart Task Distribution: `ThreadPool`
  `ThreadPool` manages a set of reusable worker threads to eliminate the overhead of spawning new threads for individual tasks.
  * **Load-Balanced Distribution**: Features automated thread selection via `getThread()`, which automatically routes new tasks to the least loaded worker thread.
  * **Cross-Thread Return Pipes**: You can execute a computing function inside a background pool thread, and `AsyncFw` will automatically capture its return value and route it back as a callback invocation inside the caller's thread context.
  
### 7. Thread-Safe Diagnostic Stream: `LogStream`
  `LogStream` provides a high-performance, thread-safe diagnostics and logging architecture utilizing strict RAII execution semantics.
  * **Automatic RAII Flushing**: Logged content is accumulated inside an internal thread-isolated buffer stream. It is guaranteed to safely flush when the temporary statement-level `LogStream` instance goes out of scope and is destroyed.
  * **Modern C++20 Formatting**: Natively supports standard stream insertion chains (`operator<<`) alongside modern, type-safe compile-time text formatting via `std::format` engines.
  
### 8. The Application Master: `MainThread`
  `MainThread` (inheriting from `Thread`) manages the application's primary boot thread (the one driving your `main()` function).
  * **Global Execution Frame**: Triggered via the blocking `MainThread::exec()`, it stalls the main function to orchestrate timers, system interrupts, and queued procedures.
  * **Transparent Qt Integration**: If the `USE_QAPPLICATION` macro is defined, `MainThread` swaps its internal poll-engine for `qApp->exec()`, mapping `AsyncFw` timers and descriptor hooks onto the native Qt event loop.
