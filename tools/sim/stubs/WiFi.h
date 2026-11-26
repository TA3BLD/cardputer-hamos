#pragma once
#include <map>
#include <string>
#include <vector>
#include "Arduino.h"
enum { WL_CONNECTED = 3, WIFI_STA = 1, WIFI_OFF = 0 };
enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
#define WIFI_SCAN_RUNNING (-1)
struct SimIp { String toString() { return "192.168.1.23"; } };
// Networks the scan finds: name, dBm, open.
struct SimNet { const char* ssid; int rssi; bool open; };
inline std::vector<SimNet> sim_wifi_nets = {{"HomeWiFi", -48, false}, {"Cafe", -71, true}, {"HomeWiFi", -80, false}, {"TA3BLD-shack", -60, false}};
struct SimWiFi {
    bool up = false;
    int wifiMode = 0;
    std::string joined;
    void disconnect() { up = false; }
    void mode(int m) { wifiMode = m; if (m == 0) up = false; }
    int getMode() { return wifiMode; }
    void setAutoReconnect(bool) {}
    void setHostname(const char*) {}
    void begin(const char* s, const char*) { up = true; joined = s; }
    int status() { return up ? WL_CONNECTED : 0; }
    SimIp localIP() { return {}; }
    String SSID() { return joined.c_str(); }
    int RSSI() { return -52; }
    int scanNetworks(bool) { return WIFI_SCAN_RUNNING; }
    int scanComplete() { return (int)sim_wifi_nets.size(); }
    String SSID(int i) { return sim_wifi_nets[(size_t)i].ssid; }
    int RSSI(int i) { return sim_wifi_nets[(size_t)i].rssi; }
    int encryptionType(int i) { return sim_wifi_nets[(size_t)i].open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK; }
    void scanDelete() {}
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
