#include "Rig.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "Ble.h"
#include "Cap.h"
#include "Config.h"
#include "Keyer.h"
#include "Lang.h"
#include "Net.h"
#include "lib/Cat.h"

namespace rig {

namespace {

enum Type { NONE, RIGCTLD, CIV, FT817, KENWOOD, YAESU, IC705 };

Type type = NONE;
WiFiClient client;                      // rigctld
bool tcpUp = false, attempted = false;
uint32_t lastAttemptMs = 0, lastPollMs = 0, lastAnswerMs = 0;
std::string line, freq, modeText;
uint8_t civAddr = 0x94;
cat::civ::Reader civIn;
cat::ascii::Reader asciiIn;
uint8_t ft817Buf[5];
int ft817Got = 0;
bool pttOn = false;

Type readType() {
    std::string t = config::get("rig_type");
    if (t == "rigctld") return RIGCTLD;
    if (t == "civ") return CIV;
    if (t == "ft817") return FT817;
    if (t == "kenwood") return KENWOOD;
    if (t == "yaesu") return YAESU;
    if (t == "ic705") return IC705;
    if (t.empty() && !config::get("rig_host").empty()) return RIGCTLD;     // older settings
    return NONE;
}

bool serialType() { return type == CIV || type == FT817 || type == KENWOOD || type == YAESU; }
bool groveKey() { return !serialType() && config::get("grove_io") == "key"; }

void setFreqText(uint64_t hz) {
    if (hz < 100000) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", hz / 1e6);
    freq = buf;
}

void send(const std::vector<uint8_t>& b) {
    if (type == IC705) ble::ic705Write(b.data(), b.size());
    else Serial2.write(b.data(), b.size());
}
void send(const std::string& s) { Serial2.write((const uint8_t*)s.data(), s.size()); }

// ---- rigctld ----
void handleLine(const std::string& l) {
    if (l.empty() || l.compare(0, 4, "RPRT") == 0) return;
    if (isdigit((unsigned char)l[0])) {
        double hz = atof(l.c_str());
        if (hz < 100000) return;            // the passband width that follows the mode
        setFreqText((uint64_t)hz);
    } else if (l == "USB" || l == "LSB") modeText = "SSB";
    else if (l == "CW" || l == "CWR") modeText = "CW";
    else if (l.compare(0, 3, "PKT") == 0) modeText = "DATA";
    else if (l == "RTTY" || l == "RTTYR") modeText = "RTTY";
    else if (l == "FM" || l == "WFM" || l == "AM") modeText = l == "WFM" ? "FM" : l;
}

void tickRigctld(uint32_t now) {
    std::string host = config::get("rig_host");
    if (host.empty() || !net::connected()) return;
    if (!tcpUp) {
        if (attempted && now - lastAttemptMs < 60000) return;
        attempted = true;
        lastAttemptMs = now;
        tcpUp = client.connect(host.c_str(), (uint16_t)atoi(config::get("rig_port", "4532").c_str()), 1500);
        lastAnswerMs = now;
        return;
    }
    if (!client.connected() || now - lastAnswerMs > 15000) {
        client.stop();
        tcpUp = false;
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

// ---- cables on the Grove port, and the IC-705 ----
void tickCat(uint32_t now) {
    if (type == IC705) {
        uint8_t buf[64];
        size_t n;
        while ((n = ble::ic705Read(buf, sizeof(buf))) > 0)
            for (size_t i = 0; i < n; i++)
                if (civIn.feed(buf[i])) {
                    lastAnswerMs = now;
                    if (civIn.freqHz) setFreqText(civIn.freqHz);
                    if (!civIn.mode.empty()) modeText = civIn.mode;
                }
    } else {
        for (int budget = 256; budget > 0 && Serial2.available(); budget--) {
            uint8_t c = (uint8_t)Serial2.read();
            if (type == CIV) {
                if (civIn.feed(c)) {
                    lastAnswerMs = now;
                    if (civIn.freqHz) setFreqText(civIn.freqHz);
                    if (!civIn.mode.empty()) modeText = civIn.mode;
                }
            } else if (type == FT817) {
                if (ft817Got < 5) ft817Buf[ft817Got++] = c;
                if (ft817Got == 5) {
                    uint64_t hz;
                    std::string m;
                    if (cat::ft817::parse(ft817Buf, hz, m)) {
                        setFreqText(hz);
                        modeText = m;
                        lastAnswerMs = now;
                    }
                    ft817Got = 6;               // ignore the rest until the next request
                }
            } else if (asciiIn.feed((char)c)) {
                lastAnswerMs = now;
                if (asciiIn.freqHz) setFreqText(asciiIn.freqHz);
                if (!asciiIn.mode.empty()) modeText = asciiIn.mode;
            }
        }
    }
    if (type == IC705 && ble::ic705State() != ble::Link::Ready) return;
    if (now - lastPollMs < 1000) return;
    lastPollMs = now;
    if (type == CIV || type == IC705) {
        send(cat::civ::readFreq(civAddr));
        send(cat::civ::readMode(civAddr));
    } else if (type == FT817) {
        ft817Got = 0;
        send(cat::ft817::readFreqMode());
    } else if (type == KENWOOD) {
        send(cat::ascii::readFreq() + cat::ascii::readMode());
    } else if (type == YAESU) {
        send(cat::ascii::readFreq() + cat::ascii::readModeYaesu());
    }
    if (now - lastAnswerMs > 5000) {
        freq.clear();
        modeText.clear();
    }
}

}  // namespace

void begin() {
    client.stop();
    tcpUp = attempted = false;
    freq.clear();
    modeText.clear();
    Serial2.end();
    ble::ic705Stop();
    type = readType();
    civAddr = (uint8_t)strtoul(config::get("civ_addr", type == IC705 ? "A4" : "94").c_str(), nullptr, 16);
    asciiIn = cat::ascii::Reader();
    asciiIn.yaesu = type == YAESU;
    asciiIn.digits = type == YAESU ? 9 : 11;
    civIn = cat::civ::Reader();
    if (serialType()) {
        long baud = atol(config::get("cat_baud", type == FT817 ? "9600" : type == CIV ? "19200" : "38400").c_str());
        Serial2.begin(baud, SERIAL_8N1, cap::GROVE_1, cap::GROVE_2);
    } else if (config::get("grove_io") == "key") {
        pinMode(cap::GROVE_1, OUTPUT);              // CW key
        pinMode(cap::GROVE_2, OUTPUT);              // PTT
        digitalWrite(cap::GROVE_1, LOW);
        digitalWrite(cap::GROVE_2, LOW);
    }
    keyer::setupPaddle();                           // after the Grove use is known
    if (type == IC705) ble::ic705Start();
    lastAnswerMs = 0;
    lastPollMs = 0;
}

void tick() {
    if (type == NONE) return;
    uint32_t now = millis();
    if (type == RIGCTLD) tickRigctld(now);
    else tickCat(now);
}

bool connected() { return type != NONE && !freq.empty(); }
std::string freqMhz() { return freq; }
std::string mode() { return modeText; }

const char* typeName() {
    switch (type) {
        case RIGCTLD: return "rigctld";
        case CIV: return "CI-V";
        case FT817: return "FT-817";
        case KENWOOD: return "Kenwood";
        case YAESU: return "Yaesu";
        case IC705: return "IC-705";
        default: return "";
    }
}

std::string status() {
    if (type == NONE) return T("No radio set (Settings: Rig type)");
    if (connected()) return std::string(typeName()) + " " + freq + " MHz " + modeText;
    if (type == IC705 && ble::ic705State() != ble::Link::Ready) return ble::ic705Status();
    if (type == RIGCTLD && !net::connected()) return T("rigctld: no WiFi");
    return std::string(typeName()) + ": " + T("no answer from the radio");
}

bool tune(double mhz, const std::string& m) {
    if (type == NONE || mhz <= 0) return false;
    uint64_t hz = (uint64_t)(mhz * 1e6 + 0.5);
    if (type == RIGCTLD) {
        if (!tcpUp) return false;
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "F %llu\n", (unsigned long long)hz);
        client.print(cmd);
        if (!m.empty()) {
            const char* hm = m == "CW" ? "CW" : m == "FM" ? "FM" : m == "AM" ? "AM" : m == "RTTY" ? "RTTY"
                           : m == "SSB" ? (mhz < 10 ? "LSB" : "USB") : "PKTUSB";
            snprintf(cmd, sizeof(cmd), "M %s 0\n", hm);
            client.print(cmd);
        }
    } else if (type == CIV || type == IC705) {
        if (type == IC705 && ble::ic705State() != ble::Link::Ready) return false;
        send(cat::civ::setFreq(civAddr, hz));
        if (!m.empty()) {
            send(cat::civ::setMode(civAddr, cat::civ::modeCode(m, mhz)));
            send(cat::civ::setData(civAddr, cat::civ::isDataMode(m)));
        }
    } else if (type == FT817) {
        send(cat::ft817::setFreq(hz));
        if (!m.empty()) send(cat::ft817::setMode(m, mhz));
    } else {
        send(cat::ascii::setFreq(hz, asciiIn.digits));
        if (!m.empty()) send(type == YAESU ? cat::ascii::setModeYaesu(m, mhz) : cat::ascii::setModeKenwood(m, mhz));
    }
    lastPollMs = 0;                         // read the new values back soon
    return true;
}

bool pttAvailable() { return groveKey() || type == CIV || type == IC705 || type == FT817 || type == KENWOOD || type == YAESU || type == RIGCTLD; }

bool ptt(bool on) {
    pttOn = on;
    if (groveKey()) {
        digitalWrite(cap::GROVE_2, on ? HIGH : LOW);
        if (type == NONE) return true;
    }
    switch (type) {
        case RIGCTLD:
            if (!tcpUp) return groveKey();
            client.print(on ? "T 1\n" : "T 0\n");
            return true;
        case CIV:
        case IC705: send(cat::civ::ptt(civAddr, on)); return true;
        case FT817: send(cat::ft817::ptt(on)); return true;
        case KENWOOD: send(cat::ascii::ptt(on, false)); return true;
        case YAESU: send(cat::ascii::ptt(on, true)); return true;
        default: return groveKey();
    }
}

bool setRepeater(double shiftMhz, double toneHz) {
    if (type == RIGCTLD) {
        if (!tcpUp) return false;
        char cmd[48];
        client.print(shiftMhz > 0 ? "R +\n" : shiftMhz < 0 ? "R -\n" : "R None\n");
        snprintf(cmd, sizeof(cmd), "O %.0f\n", std::fabs(shiftMhz) * 1e6);
        client.print(cmd);
        if (toneHz > 0) {
            snprintf(cmd, sizeof(cmd), "C %.0f\n", toneHz * 10);
            client.print(cmd);
        }
        client.print(toneHz > 0 ? "U TONE 1\n" : "U TONE 0\n");
        return true;
    }
    if (type == CIV || type == IC705) {
        send(cat::civ::frame(civAddr, {0x0F, (uint8_t)(shiftMhz > 0 ? 0x12 : shiftMhz < 0 ? 0x11 : 0x10)}));
        if (toneHz > 0) {
            int t = (int)(toneHz * 10 + 0.5);           // 88.5 Hz -> 0885 -> 00 08 85
            send(cat::civ::frame(civAddr, {0x1B, 0x00, 0x00, (uint8_t)(((t / 1000) % 10) << 4 | (t / 100) % 10),
                                           (uint8_t)(((t / 10) % 10) << 4 | t % 10)}));
        }
        send(cat::civ::frame(civAddr, {0x16, 0x42, (uint8_t)(toneHz > 0 ? 1 : 0)}));
        return true;
    }
    return false;
}

bool cwAvailable() { return type == CIV || type == IC705 || type == KENWOOD || type == RIGCTLD; }

bool sendCw(const std::string& text) {
    if (text.empty()) return false;
    if (type == CIV || type == IC705) {
        for (size_t i = 0; i < text.size(); i += 30) send(cat::civ::sendCw(civAddr, text.substr(i, 30)));
        return true;
    }
    if (type == KENWOOD) {
        for (size_t i = 0; i < text.size(); i += 24) send(cat::ascii::sendCw(text.substr(i, 24)));
        return true;
    }
    if (type == RIGCTLD && tcpUp) {
        client.print(("b " + text + "\n").c_str());
        return true;
    }
    return false;
}

}  // namespace rig
