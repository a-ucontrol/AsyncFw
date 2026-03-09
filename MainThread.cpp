#include "MainThread.h"

using namespace AsyncFw;

MainThread::Instance MainThread::instance_ __attribute__((init_priority(AsyncFw_STATIC_INIT_PRIORITY + 3)));

MainThread::Instance::Instance() { set(&mainThread_); }
