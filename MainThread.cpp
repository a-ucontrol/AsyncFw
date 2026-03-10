#include "MainThread.h"

using namespace AsyncFw;

Instance<MainThread> MainThread::instance_ __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 3))) {"MainThread"};
