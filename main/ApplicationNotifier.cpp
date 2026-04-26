#include "../core/LogStream.h"
#include "ApplicationNotifier.h"

using namespace AsyncFw;

Instance<ApplicationNotifier> ApplicationNotifier::instance_ {"ApplicationNotifier"};

ApplicationNotifier::ApplicationNotifier() {
  if (!instance_.value) instance_.value = this;
  else { logEmergency("Only one ApplicationNotifier can exist"); }
  lsTrace();
}

AsyncFw::ApplicationNotifier::~ApplicationNotifier() {
  instance_.value = nullptr;
  lsTrace();
}
