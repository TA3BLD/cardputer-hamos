// Small platform helpers.
#pragma once
#include <cstddef>

namespace sys {
size_t freeHeap();          // bytes of RAM free right now
size_t largestBlock();      // the biggest piece that can be allocated at once
size_t minFreeHeap();       // the lowest free RAM since start
}
