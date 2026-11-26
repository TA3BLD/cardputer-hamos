#pragma once
#include <utility>
#include <vector>
#include "Arduino.h"
// UDP datagrams the script queued for HamOS to receive.
inline std::deque<std::string> sim_udp;
// Datagrams HamOS sent: host:port and bytes.
inline std::vector<std::pair<std::string, std::string>> sim_udp_sent;
struct WiFiUDP {
    std::string current, out, dest;
    int beginPacket(const char* host, uint16_t port) { dest = std::string(host) + ":" + std::to_string(port); out.clear(); return 1; }
    size_t write(const uint8_t* d, size_t n) { out.append((const char*)d, n); return n; }
    int endPacket() { sim_udp_sent.push_back({dest, out}); return 1; }
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
