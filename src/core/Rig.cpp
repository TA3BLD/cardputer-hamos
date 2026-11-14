#include "Rig.h"
#include <WiFi.h>
#include <cstdio>
#include <cstdlib>
#include "Config.h"
#include "Net.h"

namespace rig {

static WiFiClient client;
static bool isConnected = false, attempted = false;
static uint32_t lastAttemptMs = 0, lastPollMs = 0, lastAnswerMs = 0;
static std::string line, freq, modeText;

void begin() {
    client.stop();
    isConnected = attempted = false;
    freq.clear();
    modeText.clear();
}

static void handleLine(const std::string& l) {
    if (l.empty() || l.compare(0, 4, "RPRT") == 0) return;
    if (isdigit((unsigned char)l[0])) {
        double hz = atof(l.c_str());
        if (hz < 100000) return;            // the passband width that follows the mode
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", hz / 1e6);
        freq = buf;
    } else if (l == "USB" || l == "LSB") modeText = "SSB";
    else if (l == "CW" || l == "CWR") modeText = "CW";
    else if (l.compare(0, 3, "PKT") == 0) modeText = "DATA";
    else if (l == "RTTY" || l == "RTTYR") modeText = "RTTY";
    else if (l == "FM" || l == "WFM" || l == "AM") modeText = l == "WFM" ? "FM" : l;
}

void tick() {
    std::string host = config::get("rig_host");
    if (host.empty() || !net::connected()) return;
    uint32_t now = millis();
    if (!isConnected) {
        if (attempted && now - lastAttemptMs < 60000) return;
        attempted = true;
        lastAttemptMs = now;
        isConnected = client.connect(host.c_str(), (uint16_t)atoi(config::get("rig_port", "4532").c_str()), 1500);
        lastAnswerMs = now;
        return;
    }
    if (!client.connected() || now - lastAnswerMs > 15000) {
        client.stop();
        isConnected = false;
        freq.clear();
        modeText.clear();
        lastAttemptMs = now;
        return;
    }
    if (now - lastPollMs > 3000) {
        lastPollMs = now;
        client.print("f\nm\n");
    }
    for (int budget = 128; budget > 0 && client.available(); budget--) {
        char c = (char)client.read();
        lastAnswerMs = now;
        if (c == '\n') { handleLine(line); line.clear(); }
        else if (c != '\r' && line.size() < 40) line += c;
    }
}

bool connected() { return isConnected && !freq.empty(); }

// rigctld mode name for a logbook / spot mode.
static const char* hamlibMode(const std::string& m, double mhz) {
    if (m == "CW") return "CW";
    if (m == "FM") return "FM";
    if (m == "AM") return "AM";
    if (m == "RTTY") return "RTTY";
    if (m == "SSB" || m == "PHONE") return mhz < 10 ? "LSB" : "USB";
    if (m.empty()) return "";
    return "PKTUSB";                        // FT8, FT4, PSK31, DATA ...
}

bool tune(double mhz, const std::string& m) {
    if (!isConnected || mhz <= 0) return false;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "F %.0f\n", mhz * 1e6);
    client.print(cmd);
    const char* hm = hamlibMode(m, mhz);
    if (*hm) {
        snprintf(cmd, sizeof(cmd), "M %s 0\n", hm);
        client.print(cmd);
    }
    lastPollMs = 0;                         // read the new values back soon
    return true;
}
std::string freqMhz() { return freq; }
std::string mode() { return modeText; }

}  // namespace rig
