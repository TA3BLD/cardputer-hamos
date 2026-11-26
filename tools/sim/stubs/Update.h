#pragma once
#include "Arduino.h"
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
// Records what would be flashed; sim_update_valid decides whether end() accepts it.
inline bool sim_update_valid = true;
struct SimUpdate {
    std::string data;
    bool started = false, finished = false, aborted = false;
    bool begin(size_t) { data.clear(); started = true; finished = aborted = false; return true; }
    size_t write(uint8_t* d, size_t n) { data.append((const char*)d, n); return n; }
    bool end(bool) { finished = sim_update_valid; return sim_update_valid; }
    void abort() { aborted = true; }
};
inline SimUpdate Update;
