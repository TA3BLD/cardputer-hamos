#include "System.h"
#include <Arduino.h>

namespace sys {

#ifdef HAMOS_SIM
size_t freeHeap() { return 300000; }
size_t largestBlock() { return 110000; }
size_t minFreeHeap() { return 240000; }
const char* resetReason() { return "power on"; }
bool resetWasFault() { return false; }
#else
size_t freeHeap() { return ESP.getFreeHeap(); }
size_t largestBlock() { return ESP.getMaxAllocHeap(); }
size_t minFreeHeap() { return ESP.getMinFreeHeap(); }
const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_SW: return "restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT: return "watchdog (int)";
        case ESP_RST_TASK_WDT: return "watchdog (task)";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "low voltage";
        case ESP_RST_DEEPSLEEP: return "wake from sleep";
        case ESP_RST_EXT: return "reset pin";
        default: return "unknown";
    }
}
bool resetWasFault() {
    esp_reset_reason_t r = esp_reset_reason();
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
           r == ESP_RST_BROWNOUT;
}
#endif

}  // namespace sys
