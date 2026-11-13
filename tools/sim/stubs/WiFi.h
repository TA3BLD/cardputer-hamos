#pragma once
enum { WL_CONNECTED = 3, WIFI_STA = 1 };
struct SimWiFi {
    bool up = false;
    void disconnect() { up = false; }
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void begin(const char*, const char*) { up = true; }
    int status() { return up ? WL_CONNECTED : 0; }
};
inline SimWiFi WiFi;
