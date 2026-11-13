#pragma once
#include <map>
#include "Arduino.h"
enum { WL_CONNECTED = 3, WIFI_STA = 1 };
struct SimIp { String toString() { return "192.168.1.23"; } };
struct SimWiFi {
    bool up = false;
    void disconnect() { up = false; }
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void begin(const char*, const char*) { up = true; }
    int status() { return up ? WL_CONNECTED : 0; }
    SimIp localIP() { return {}; }
};
inline SimWiFi WiFi;

// TCP "servers", one per host name: sim_net[host] is what the server sends,
// sim_net_sent[host] collects what HamOS sent to it.
inline std::map<std::string, SimStream> sim_net;
inline std::map<std::string, std::string> sim_net_sent;
struct WiFiClient {
    std::string host;
    bool open = false;
    int connect(const char* h, uint16_t, int32_t = 0) { host = h; open = true; return 1; }
    bool connected() { return open; }
    int available() { return open ? sim_net[host].available() : 0; }
    int read() { return sim_net[host].read(); }
    void print(const char* s) { sim_net_sent[host] += s; }
    void stop() { open = false; }
};
