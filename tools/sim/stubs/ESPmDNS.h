#pragma once
// mDNS: nothing to announce in the simulator.
struct SimMdns {
    bool begin(const char*) { return true; }
    void end() {}
    void addService(const char*, const char*, int) {}
};
inline SimMdns MDNS;
