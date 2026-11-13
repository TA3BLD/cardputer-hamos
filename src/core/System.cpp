#include "System.h"
#include <Arduino.h>

namespace sys {

size_t freeHeap() {
#ifdef HAMOS_SIM
    return 300000;
#else
    return ESP.getFreeHeap();
#endif
}

}  // namespace sys
