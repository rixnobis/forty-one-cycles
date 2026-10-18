#pragma once

#include "common/syscalls/syscalls.h"

// Serial trace. The printf blocks until the host reads, so a release build
// (-DNO_LOG) must not carry it: with no reader on the port the demo stops.
#ifdef NO_LOG
#define DEMO_LOG(...) \
    do {              \
    } while (0)
#else
#define DEMO_LOG(...) ramsyscall_printf(__VA_ARGS__)
#endif
