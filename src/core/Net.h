// WiFi + NTP. Non-blocking: call tick() from the main loop.
#pragma once
#include <string>

namespace net {
void begin();        // (re)connect using the wifi_ssid / wifi_pass settings
void tick();
bool configured();
bool connected();
bool timeValid();    // true once the clock has been set (NTP or GPS)
std::string address();   // this device's IP address, "" when not connected
}
