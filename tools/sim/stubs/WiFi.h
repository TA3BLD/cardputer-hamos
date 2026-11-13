#pragma once
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

// Telnet "server": whatever the script puts in sim_telnet arrives as received data.
inline SimStream sim_telnet;
struct WiFiClient {
    bool open = false;
    int connect(const char*, uint16_t, int32_t = 0) { open = true; return 1; }
    bool connected() { return open; }
    int available() { return sim_telnet.available(); }
    int read() { return sim_telnet.read(); }
    void print(const char*) {}
    void stop() { open = false; }
};
