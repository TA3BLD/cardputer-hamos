#pragma once
#include "Arduino.h"
// UDP datagrams the script queued for HamOS to receive.
inline std::deque<std::string> sim_udp;
struct WiFiUDP {
    std::string current;
    uint8_t begin(uint16_t) { return 1; }
    void stop() {}
    int parsePacket() {
        if (sim_udp.empty()) return 0;
        current = sim_udp.front();
        sim_udp.pop_front();
        return (int)current.size();
    }
    int read(uint8_t* buf, size_t len) {
        size_t n = current.size() < len ? current.size() : len;
        memcpy(buf, current.data(), n);
        return (int)n;
    }
};
