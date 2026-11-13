#include "Net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ctime>
#include <sys/time.h>
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
long long utcMillis() {
#ifdef HAMOS_SIM
    return sim_utc_ms();
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

std::string address() { return connected() ? std::string(WiFi.localIP().toString().c_str()) : ""; }

}  // namespace net
