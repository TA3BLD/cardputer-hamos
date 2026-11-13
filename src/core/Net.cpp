#include "Net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ctime>
#include "Config.h"

namespace net {

static bool hasCredentials = false;
static bool ntpStarted = false;

void begin() {
    std::string ssid = config::get("wifi_ssid");
    std::string pass = config::get("wifi_pass");
    hasCredentials = !ssid.empty();
    ntpStarted = false;
    WiFi.disconnect();
    if (!hasCredentials) return;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void tick() {
    if (!ntpStarted && connected()) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");   // system clock in UTC
        ntpStarted = true;
    }
}

bool configured() { return hasCredentials; }
bool connected() { return WiFi.status() == WL_CONNECTED; }
bool timeValid() { return time(nullptr) > 1700000000; }

}  // namespace net
