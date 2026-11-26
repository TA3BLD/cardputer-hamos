// Small platform helpers.
#pragma once
#include <cstddef>

namespace sys {
size_t freeHeap();          // bytes of RAM free right now
size_t largestBlock();      // the biggest piece that can be allocated at once
size_t minFreeHeap();       // the lowest free RAM since start
// Why the device last started: "power on", "crash", "watchdog", "low voltage"...
const char* resetReason();
bool resetWasFault();       // crash, watchdog or brown-out (not power on / restart)
}
