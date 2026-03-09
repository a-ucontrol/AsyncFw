#include "MainThread.h"

using namespace AsyncFw;

MainThread MainThread::mainThread_ __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 3)));
