// WiFi + NTP. Non-blocking: call tick() from the main loop.
#pragma once
#include <string>
#include <vector>

namespace net {
void begin();        // (re)connect using the wifi_ssid / wifi_pass settings
void off();          // radio off to save the battery (begin() turns it on again)
void tick();
bool configured();
bool connected();
bool timeValid();    // true once the clock has been set (NTP or GPS)
std::string address();   // this device's IP address, "" when not connected
std::string hostname();  // name on the local network ("hamos" -> http://hamos.local)
std::string ssid();      // network joined, "" when not connected
int rssi();              // signal in dBm, 0 when not connected

// Looking for networks (takes a few seconds, in the background).
struct Network {
    std::string ssid;
    int rssi = 0;
    bool open = false;      // no password
};
void scanStart();
bool scanning();
const std::vector<Network>& scanResults();     // strongest first, each name once
long long utcMillis();   // milliseconds since 1970 (UTC); only meaningful when timeValid()
}
