/*
Copyright (c) 2019-2026 Alexandr Kuzmuk

This file is part of the AsyncFw project. Licensed under the MIT License.
See {Link: LICENSE file https://mit-license.org} in the project root for full license information.
*/

//! [snippet]
#include <AsyncFw/MainThread>
#include <AsyncFw/LogStream>

class ExampleClass {
public:
  static inline ExampleClass *instance() { return instance_.value; }
  ExampleClass(const std::string &name) : name_(name) { lsDebug() << name_; }
  ~ExampleClass() {
    if (instance_.value == this) instance_.value = nullptr;
    lsDebug() << name_;
  }
  const std::string &name() { return name_; }

private:
  static inline class Instance : public AsyncFw::Instance<ExampleClass> {
  public:
    using AsyncFw::Instance<ExampleClass>::Instance;
    void created() override { lsInfoGreen() << value->name(); }
  } instance_ {"ExampleClass"};
  std::string name_;
};

int main(int argc, char *argv[]) {
  ExampleClass _e("ExampleClass");
  AsyncFw::Instance<ExampleClass>::create("ExampleClassInstance");
  AsyncFw::Thread::current()->invokeMethod([]() { AsyncFw::MainThread::exit(); });

  lsInfoMagenta() << _e.name();
  lsInfoMagenta() << ExampleClass::instance()->name();

  lsNotice() << "Start Applicaiton" << std::endl;
  int ret = AsyncFw::MainThread::exec();
  lsNotice() << "End Applicaiton " << ret;
  return ret;
}
//! [snippet]
