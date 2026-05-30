# AsyncFw

Async Framework is a lightweight, high-performance C++20 runtime that brings asynchronous programming to your applications. It features built-in support for timers, poll notifiers (event-driven I/O), thread-safe signals/slots, sockets, and native C++20 coroutines.

AsyncFw allows you to run long-running operations concurrently across multiple threads without ever blocking your primary event loop.

See the [GitHub repository], [documentation] and [examples] for details.

[GitHub repository]: https://github.com/a-ucontrol/AsyncFw
[documentation]: https://a-ucontrol.github.io/AsyncFw
[examples]: https://a-ucontrol.github.io/AsyncFw/examples.html

---

### 1. The Core Backbone: `AbstractThread`
`AbstractThread` is the foundational element of the entire framework. It goes beyond a simple `std::thread` wrapper by maintaining its own **Task Queue** and a localized **Event Loop (Reactor)**.
* **The Reactor Loop**: Internally, `AbstractThread::exec()` runs a highly optimized polling loop. On Linux, it leverages **`epoll`** (`epoll_wait`); on other systems, it falls back to standard **`poll`**. 
* **Non-Blocking Inter-Thread Wakeup**: When a foreign thread posts a task via `.invoke()`, it inserts the task into a synchronized queue and wakes up the target thread's blocking `epoll_wait` instantly. On Linux, this is achieved via **`eventfd`** (`EVENTFD_WAKE`), which introduces near-zero overhead compared to traditional pipe-based signaling.
* **Intelligent Nested Loops**: It handles nested invocation processing safely (`nested_` tracking). If an inner loop is executed, it can yield to process waiting asynchronous callbacks, prevent deadlines from slipping, and protect against stack overflow or event deadlocks.
* **Deterministic Graceful Shutdown**: Using `requestInterrupt()` and `waitInterrupted()`, a thread transitions through safe structural states, ensuring that pending items in the pipeline are safely flushed before resources are released.

### 2. Deep Coroutine Orchestration & RPC: `coInvoke`
`AsyncFw` supercharges C++20 coroutines by tying their suspension hooks directly into the framework's cross-thread architecture via `CoroutineTask` and generic awaiters (`CoroutineAwait`, `CoroutineInvokeAwait`).
* **Automated Thread Marshalling**: When a coroutine yields via `co_await`, heavy jobs are offloaded to background workers. Once done, the internal `promise_type::resume_queued()` automatically schedules continuation back onto the thread where the coroutine was originally created. This eliminates race conditions.
* **Stateful Data Transport**: The coroutine `promise_type` inherits from `AnyData`, turning the underlying compiler-generated coroutine frame into a dynamic data store. Background tasks push computed results directly into the frame, which are then transparently unpacked and yielded during `await_resume`.
* **The `coInvoke` Engine**: Provides an elite, non-blocking remote procedure call (RPC) syntax. Developers can await free functions, lambdas, or class member methods using `co_await coInvoke(...)`. The framework schedules execution on either the global `ThreadPool` or a specific target thread event loop, returning the computed result linearly.
* **Synchronous Joining (`wait()`)**: Allows developers to block-and-wait for an active coroutine using `task.wait()`. This seamlessly drives an internal nested event frame (`AbstractThread::Holder`) to process other interleaved callbacks without blocking system resources.

### 3. Network Worker Thread: `Thread` & Secure Sockets
`Thread` inherits from `AbstractThread` and specializes in managing network connections, asynchronous sockets (`AbstractSocket`), and connection pools.
* **Socket Lifecycle Management**: Acts as an RAII container for asynchronous sockets (`sockets_`). When a `Thread` instance is destroyed, it automatically interrupts its own execution loop, safely waits for finish, and gracefully closes and deletes all registered sockets to prevent file descriptor leaks.
* **Robust Crash Prevention (`SIGPIPE`)**: On POSIX/Unix systems, the worker automatically blocks `SIGPIPE` at startup. This prevents the entire application from terminating abruptly if a remote client abruptly disconnects during a socket operation or a TLS handshake.
* **TLS/SSL Cryptographic Layer**: Through `AbstractTlsSocket`, the framework builds a secure layer on top of standard network sockets. It natively handles session initiation, encrypted packet streams, and cryptographic handshakes.

### 4. Cryptographic State & PKI: `TlsContext`
`TlsContext` acts as the secure state machine wrapping OpenSSL (`SSL_CTX`), decoupling TLS configuration logic from network I/O routines.
* **Automated OpenSSL Lifecycle**: Uses a reference-counted `Private` implementation to manage raw OpenSSL pointers, guaranteeing that resources like `SSL_CTX_free` are safely called when all instances of a context drop out of scope.
* **On-the-Fly PKI Generation**: Natively provides capability to generate RSA keys (`generateKey`), export certificate signing requests (`generateRequest`), and sign x509 certificates (`signRequest`). This enables applications to act as a micro-Certificate Authority (CA) or generate self-signed profiles on demand.
* **Granular Certificate Control**: Features integrated peer verification hooks with capability to dynamically adjust validation rules, such as suppressing certificate expiration errors (`IgnoreErrors::TimeValidity`) for internal system environments.

### 5. Smart Task Distribution: `ThreadPool`
`ThreadPool` manages a set of reusable worker threads to eliminate the overhead of spawning new threads for individual tasks.
* **Load-Balanced Distribution**: Features automated thread selection via `getThread()`, which automatically routes new tasks to the least loaded worker thread based on current queue metrics.
* **Cross-Thread Return Pipes**: Provides advanced `ThreadPool::async` templates. You can execute a computing function inside a background pool thread, and `AsyncFw` will automatically capture its return value and route it back as a callback invocation inside the caller's originating thread context.

### 6. Thread-Safe Diagnostic Stream: `LogStream`
`LogStream` provides a high-performance, thread-safe diagnostics and logging architecture utilizing strict RAII execution semantics.
* **Automatic RAII Flushing**: Logged content is accumulated inside an internal thread-isolated buffer stream. It is guaranteed to safely flush to console or designated logging targets automatically when the temporary statement-level `LogStream` instance goes out of scope and is destroyed.
* **Modern C++20 Formatting**: Natively supports standard stream insertion chains (`operator<<`) alongside modern, type-safe compile-time text formatting via `std::format` engines.
* **Compile-Time Optimization**: Allows critical log isolation level rules (`LS_NO_TRACE`, `LS_NO_DEBUG`) to minimize runtime performance overhead in production environments.

### 7. The Application Master: `MainThread`
`MainThread` (inheriting from `Thread`) manages the application's primary boot thread (the one driving your `main()` function).
* **Global Execution Frame**: Triggered via the blocking `MainThread::exec()`, it stalls the main function to orchestrate timers, system interrupts, and queued procedures until an application-wide shutdown is requested via `MainThread::exit()`.
* **Transparent Qt Integration**: If the `USE_QAPPLICATION` macro is defined, `MainThread` swaps its internal poll-engine for `qApp->exec()`. This maps `AsyncFw` timers and descriptor hooks onto the native Qt event loop, allowing you to use this framework inside **Qt GUI applications** without modification.

---

## Usage Examples

### 1. Thread-Safe Publisher-Subscriber (`FunctionConnector`)

`FunctionConnector` provides a synchronized signaling system. By utilizing the `Protected` variant, you enforce strict encapsulation: **only the owner class can emit the signal**, while any external component can safely connect to it using a `const` reference.

```cpp
#include <AsyncFw/FunctionConnector>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

// A target class to demonstrate member-function binding
class MethodConnectionExample {
public:
  void method(int val, const std::string &str) { 
    lsNotice() << "Method receiver processed value:" << val << str; 
  }
};

class Sender {
public:
  Sender() {
    // 1. Connect a callback to an internal high-resolution timer
    timer.timeout.connect([this]() {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      logInfo() << cnt << " send from thread: " << ct->name() << " (ID: " << ct->id() << ")";
      
      // 2. Safely trigger event propagation across subscribers
      connector(cnt++, "");
      
      // 3. Gracefully stop the application loop when conditions are met
      if (cnt == 10) AsyncFw::MainThread::exit(0);
    });
    timer.start(10); // Interval set to 10 milliseconds
  }

  // Encapsulated Connector: Only 'Sender' can invoke this object as a function.
  // External entities are restricted to calling '.connect()' on it.
  AsyncFw::FunctionConnector<int, const std::string &>::Protected<Sender> connector;

private:
  int cnt = 0;
  AsyncFw::Timer timer;
};

class Receiver {
public:
  // Receives a const reference. The '.connect()' method is non-mutating (const-qualified)
  Receiver(const std::string &name, const Sender &sender) {
    fcg = sender.connector.connect([name_ = name](int val, const std::string &) {
      AsyncFw::AbstractThread *ct = AsyncFw::AbstractThread::current();
      
      // Thread-safe dispatching guarantees that this runs on the Receiver's thread execution context
      logInfo() << name_ << " received value " << val << " inside thread: " << ct->name();
    });
  }
  
  // RAII Guard: Automatically disconnects the slot from the publisher when this instance drops out of scope
  AsyncFw::FunctionConnectionGuard fcg;
};

int main(int argc, char *argv[]) {
  Sender *sender;

  // Spin up a dedicated worker thread to process timer ticks and state changes
  AsyncFw::Thread thread("SenderThread");
  thread.start();
  
  // Safely instantiate the Sender class directly within the context of the newly spawned thread
  thread.invoke([&sender]() {
    sender = new Sender;  
  }, true);

  // Instantiate receivers bound to the MainThread, referencing the thread-isolated Sender
  Receiver receiver1("R1", *sender);
  Receiver receiver2("R2", *sender);

  // Example A: Binding a standard C++ class member pointer
  MethodConnectionExample _e;
  sender->connector.connect(&MethodConnectionExample::method, &_e);

  // Example B: Binding a standard ad-hoc Lambda expression
  auto lambda = [](int val, const std::string &) { 
    lsNotice() << "Anonymous lambda received event value:" << val; 
  };
  sender->connector.connect(lambda);

  logNotice() << "Start Application Loop...";

  // Enter the blocking master execution frame. Dispatches tasks and polls descriptors.
  int ret = AsyncFw::MainThread::exec();

  logNotice() << "Application Loop terminated cleanly with code:" << ret;
  
  delete sender;
  return ret;
}
```

---

### 2. Asynchronous Timers with C++20 Coroutines (`co_await`)

Instead of trapping your code inside nested "Callback Hell", AsyncFw natively extends standard C++20 Coroutines. You can write sequential, easy-to-read business logic that suspends (`co_await`) execution without halting or blocking the underlying OS thread.

```cpp
#include <chrono>
#include <AsyncFw/MainThread>
#include <AsyncFw/Timer>
#include <AsyncFw/LogStream>

// An asynchronous coroutine task. 
// It yields execution back to the loop when waiting, and automatically resumes on the correct thread.
AsyncFw::CoroutineTask task(AsyncFw::Timer *timer) {
  lsDebug() << "Coro step 1 executed at time:" << std::chrono::system_clock::now();
  
  // The coroutine suspends here for 10ms. The MainThread stays free to handle other events.
  co_await timer->coTimeout(10);
  lsDebug() << "Coro step 2 (resumed) executed at time:" << std::chrono::system_clock::now();
  
  // Suspend again for another 10ms
  co_await timer->coTimeout(10);
  lsDebug() << "Coro step 3 (resumed) executed at time:" << std::chrono::system_clock::now();
  
  // Gracefully initiate shutdown of the core loop
  AsyncFw::MainThread::exit(0);
}

int main(int argc, char *argv[]) {
  AsyncFw::Timer timer1;
  timer1.start(10);
  AsyncFw::Timer timer2;

  // Classic asynchronous callbacks can easily run alongside active coroutines
  timer1.timeout.connect([]() { 
    lsDebug() << std::chrono::system_clock::now() << " [Callback] Timer 1 tick"; 
  });

  timer2.timeout.connect([]() { 
    lsDebug() << std::chrono::system_clock::now() << " [Callback] Timer 2 tick"; 
  });

  // Launch the coroutine. This registers the task into the runtime loop and returns immediately.
  task(&timer2);

  logNotice() << "Start Application Loop...";
  
  // Drives both the coroutine resume-points and the standard timer loops simultaneously
  int ret = AsyncFw::MainThread::exec();
  
  logNotice() << "End Application Loop. Code:" << ret;
  return ret;
}
```
