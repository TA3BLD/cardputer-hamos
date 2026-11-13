#pragma once
#include "Arduino.h"
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
struct SimUpdate {
    bool begin(size_t) { return true; }
    size_t write(uint8_t*, size_t n) { return n; }
    bool end(bool) { return true; }
    void abort() {}
};
inline SimUpdate Update;
