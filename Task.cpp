#include "Task.h"
#include "core/LogStream.h"

AsyncFw::AbstractTask::AbstractTask() { lsTrace(); }
AsyncFw::AbstractTask::~AbstractTask() { lsTrace(); }
