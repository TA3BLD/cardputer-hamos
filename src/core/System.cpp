#include "System.h"
#include <Arduino.h>

namespace sys {

#ifdef HAMOS_SIM
size_t freeHeap() { return 300000; }
size_t largestBlock() { return 110000; }
size_t minFreeHeap() { return 240000; }
#else
size_t freeHeap() { return ESP.getFreeHeap(); }
size_t largestBlock() { return ESP.getMaxAllocHeap(); }
size_t minFreeHeap() { return ESP.getMinFreeHeap(); }
#endif

}  // namespace sys
