#include "Net.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <ctime>
#include <sys/time.h>
#include "Config.h"

namespace net {

static bool hasCredentials = false;
static bool ntpStarted = false;
static bool mdnsStarted = false;
static bool scanRunning = false;
static bool resumeAfterScan = false;
static std::vector<Network> found;

std::string hostname() {
    std::string h;
    for (char c : config::get("hostname", "hamos")) {       // letters, digits and '-' only
        if (isalnum((unsigned char)c)) h += (char)tolower((unsigned char)c);
        else if (c == '-' && !h.empty()) h += c;
    }
    return h.empty() ? "hamos" : h;
}

void begin() {
    std::string ssid = config::get("wifi_ssid");
    std::string pass = config::get("wifi_pass");
    hasCredentials = !ssid.empty();
    ntpStarted = false;
    if (mdnsStarted) MDNS.end();
    mdnsStarted = false;
    WiFi.disconnect();
    if (!hasCredentials) return;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname().c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void off() {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    hasCredentials = false;
}

void tick() {
    if (!ntpStarted && connected()) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");   // system clock in UTC
        ntpStarted = true;
    }
    // http://hamos.local on the network (mDNS); started again after the link comes back.
    static uint32_t mdnsTried = 0;
    if (connected() && !mdnsStarted && (mdnsTried == 0 || millis() - mdnsTried > 10000)) {
        mdnsTried = millis() | 1;
        mdnsStarted = MDNS.begin(hostname().c_str());
        if (mdnsStarted) MDNS.addService("http", "tcp", 80);
    } else if (!connected() && mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
    }
    if (scanRunning) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        scanRunning = false;
        found.clear();
        for (int i = 0; i < n; i++) {
            Network w;
            w.ssid = WiFi.SSID(i).c_str();
            w.rssi = WiFi.RSSI(i);
            w.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
            if (w.ssid.empty()) continue;                       // hidden network
            bool seen = false;
            for (Network& o : found)
                if (o.ssid == w.ssid) {
                    seen = true;
                    if (w.rssi > o.rssi) o = w;
                }
            if (!seen) found.push_back(w);
        }
        std::sort(found.begin(), found.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
        WiFi.scanDelete();
        if (resumeAfterScan) {
            resumeAfterScan = false;
            if (!connected()) begin();      // back to trying the saved network
        }
    }
}

void scanStart() {
    if (scanRunning) return;
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    // Still trying to join a network (maybe a wrong name or password): those
    // attempts make the scan fail, so pause them for the scan.
    if (hasCredentials && !connected()) {
        WiFi.disconnect();
        resumeAfterScan = true;
    }
    scanRunning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
}
bool scanning() { return scanRunning; }
const std::vector<Network>& scanResults() { return found; }
std::string ssid() { return connected() ? std::string(WiFi.SSID().c_str()) : ""; }
int rssi() { return connected() ? (int)WiFi.RSSI() : 0; }

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
