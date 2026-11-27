#include "Keyer.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <cstring>
#include <deque>
#include <utility>
#include "Lang.h"
#include "Cap.h"
#include "Config.h"
#include "Gps.h"
#include "Lora.h"
#include "Rig.h"
#include "lib/Iambic.h"

namespace keyer {

namespace {

struct Step {
    bool down;
    uint32_t ms;
    uint16_t letter = 0;        // where in the text (for the TX display)
    int8_t element = -1;
};
// The text being sent, for txView().
std::string txText;
bool txActive = false, txEstimated = false;
uint32_t txStartMs = 0, txDoneMs = 0;
size_t txLetter = 0;
int txElement = -1;
bool txDown = false;
std::deque<Step> steps;             // the text being sent, as key down / up times
uint32_t stepEnd = 0;
bool keyIsDown = false, stepping = false;
uint32_t radioBusyUntil = 0;
int speed = 0;
morse::Iambic iambic;
morse::StraightKey straight;
bool paddleDown = false;
uint32_t paddleToneMs = 0;
std::string paddleLetters, paddleNew, pendingWord;

// The paddle / key input, from the "paddle_*" settings.
enum class KeyMode { IAMBIC, SIMPLE, STRAIGHT };
struct PaddleSetup {
    bool wanted = false, working = false;
    int dit = -1, dah = -1;             // dah = -1 for a straight key
    bool high = false;                  // contacts to 3V3 (pull-down) instead of to GND
    bool swap = false;
    bool txAll = true;                  // keys the radio on every page, not only in the CW keyer
    KeyMode mode = KeyMode::IAMBIC;
    std::string info;
};
PaddleSetup pad;
bool practicing = false;
bool keyerPage = false;                 // the CW keyer is open
uint32_t airUntil = 0;                  // the radio counts as keyed until then (status bar TX)

// The paddle keys the radio: never in Morse practice; in the CW keyer always;
// elsewhere when "Paddle TX on all pages" is on.
bool paddleTransmits() { return !practicing && (keyerPage || pad.txAll); }
void onAirFor(uint32_t ms) {
    uint32_t until = millis() + ms;
    if ((int32_t)(until - airUntil) > 0) airUntil = until;
}
uint32_t sendMs(const std::string& text, int w) {
    uint32_t dot = (uint32_t)morse::dotMs(w), t = 0;
    size_t l;
    int e;
    bool d;
    while (morse::timeline(text, dot, t, l, e, d)) t += dot;
    return t;
}
int practiceWpm = 18;
bool gpsReleased = false;

// Pins a paddle may use: the Grove port, and the header pins the cap uses
// (free without the cap). Not: G8/G9 (keyboard I2C), G12/G14/G39/G40 (SD card).
bool allowedPin(int p) { return p == 1 || p == 2 || (p >= 3 && p <= 6) || p == 13 || p == 15; }
bool capPin(int p) { return (p >= 3 && p <= 6) || p == 13 || p == 15; }
bool groveBusy() {
    std::string t = config::get("rig_type"), g = config::get("grove_io");
    return t == "civ" || t == "ft817" || t == "kenwood" || t == "yaesu" || g == "key" || g == "wx";
}
bool capAttached() { return lora::ready() || cap::hasExpander() || (!gpsReleased && gps::present()); }
bool readPin(int p) { return (digitalRead(p) == HIGH) == pad.high; }

// The Grove port is the key line unless a CAT cable uses it.
bool groveKey() {
    std::string t = config::get("rig_type");
    bool cable = t == "civ" || t == "ft817" || t == "kenwood" || t == "yaesu";
    return config::get("grove_io") == "key" && !cable;
}
int tone() {
    int t = atoi(config::get("cw_tone", "700").c_str());
    return t < 200 || t > 2000 ? 700 : t;
}

bool practice = false;                  // the text being sent is sidetone only

void key(bool down) {
    keyIsDown = down;
    if (groveKey() && !practice) digitalWrite(cap::GROVE_1, down ? HIGH : LOW);
}

}  // namespace

int wpm() {
    if (!speed) speed = atoi(config::get("cw_wpm", "18").c_str());
    if (speed < 5 || speed > 50) speed = 18;
    return speed;
}

void setWpm(int w) {
    speed = w < 5 ? 5 : w > 50 ? 50 : w;
    config::set("cw_wpm", std::to_string(speed));
}

const char* outputName() {
    if (groveKey()) return "Grove key";
    if (rig::cwAvailable() && rig::connected()) return "radio (CAT)";
    return "sidetone only";
}

bool paddle() { return pad.working; }
std::string paddleInfo() { return pad.info; }
bool paddleError() { return pad.wanted && !pad.working; }
bool paddleUsesCapPins() { return pad.working && (capPin(pad.dit) || capPin(pad.dah)); }

void setupPaddle() {
    PaddleSetup old = pad;
    pad = PaddleSetup();
    // "Grove key/paddle/wx" = paddle (older settings) means the paddle on G1/G2.
    bool legacy = config::get("grove_io") == "paddle";
    pad.wanted = config::get("paddle_on", "0") == "1" || legacy;
    if (old.working) {                  // let go of the old pins (not Grove pins the radio now uses)
        for (int p : {old.dit, old.dah})
            if (p >= 0 && !(p <= 2 && groveBusy())) pinMode(p, INPUT);
    }
    auto release = [&]() {
        if (gpsReleased) {
            gps::begin();               // the GNSS pins are free again
            gpsReleased = false;
        }
    };
    if (!pad.wanted) {
        release();
        return;
    }
    auto lower = [](std::string v) {
        for (char& c : v) c = (char)tolower((unsigned char)c);
        return v;
    };
    std::string mode = lower(config::get("paddle_mode", "iambic"));
    pad.mode = mode == "straight" ? KeyMode::STRAIGHT : mode == "simple" ? KeyMode::SIMPLE : KeyMode::IAMBIC;
    pad.high = lower(config::get("paddle_pull", "gnd")) == "3v3";
    pad.swap = config::get("paddle_swap", "0") == "1";
    pad.txAll = config::get("paddle_tx_all", "1") != "0";
    pad.dit = atoi(config::get("paddle_dit", "1").c_str());
    pad.dah = pad.mode == KeyMode::STRAIGHT ? -1 : atoi(config::get("paddle_dah", "2").c_str());
    char b[64];
    auto fail = [&](const char* why) {
        pad.info = why;
        release();
    };
    if (!allowedPin(pad.dit) || (pad.dah >= 0 && !allowedPin(pad.dah))) return fail(T("Paddle: pins 1 2 3 4 5 6 13 15 only"));
    if (pad.dit == pad.dah) return fail(T("Paddle: dit and dah need two pins"));
    bool grove = pad.dit <= 2 || (pad.dah >= 0 && pad.dah <= 2);
    if (grove && !legacy && groveBusy()) return fail(T("Paddle: Grove is used (radio cable / key / WX)"));
    bool header = capPin(pad.dit) || capPin(pad.dah);
    if (header && capAttached()) return fail(T("Paddle: cap attached, use Grove pins 1 2"));
    // The GNSS serial port lets go of G13 / G15 for the paddle.
    bool gnss = pad.dit == cap::GPS_RX || pad.dit == cap::GPS_TX || pad.dah == cap::GPS_RX || pad.dah == cap::GPS_TX;
    if (gnss && !gpsReleased) {
        Serial1.end();
        gpsReleased = true;
    } else if (!gnss) {
        release();
    }
    pinMode(pad.dit, pad.high ? INPUT_PULLDOWN : INPUT_PULLUP);
    if (pad.dah >= 0) pinMode(pad.dah, pad.high ? INPUT_PULLDOWN : INPUT_PULLUP);
    pad.working = true;
    const char* m = pad.mode == KeyMode::STRAIGHT ? T("straight key") : pad.mode == KeyMode::SIMPLE ? T("simple") : T("iambic");
    if (pad.dah < 0) snprintf(b, sizeof(b), "G%d %s %s", pad.dit, pad.high ? "3V3" : "GND", m);
    else snprintf(b, sizeof(b), "G%d/G%d %s %s%s", pad.dit, pad.dah, pad.high ? "3V3" : "GND", m, pad.swap ? " ><" : "");
    pad.info = b;
    iambic = morse::Iambic();
    straight = morse::StraightKey();
    straight.setWpm(practicing ? practiceWpm : wpm());
    paddleDown = false;
}

void setKeyerPage(bool open) { keyerPage = open; }
bool onAir() { return airUntil && (int32_t)(airUntil - millis()) > 0; }

void setPractice(bool on, int w) {
    practicing = on;
    if (w >= 5 && w <= 50) practiceWpm = w;
    straight.setWpm(on ? practiceWpm : wpm());
    pendingWord.clear();
    paddleNew.clear();
}

void send(const std::string& textIn, bool sidetoneOnly) {
    std::string text;
    for (char c : textIn) text += (char)toupper((unsigned char)c);
    if (text.empty()) return;
    practice = sidetoneOnly;
    int dot = morse::dotMs(wpm());
    if (!txActive || steps.empty()) {           // a new text (more while sending is queued behind it)
        txText.clear();
        txLetter = 0;
        txElement = -1;
    }
    size_t base = txText.size();
    txText += text;
    txActive = true;
    txDoneMs = 0;
    if (!sidetoneOnly && !groveKey() && rig::cwAvailable() && rig::connected()) {
        rig::sendCw(text);
        txText = text;                          // the radio times it: estimated from the start
        txEstimated = true;
        txStartMs = millis();
        radioBusyUntil = txStartMs + 50 + sendMs(text, wpm());
        onAirFor(radioBusyUntil - txStartMs + 1000);
        return;
    }
    txEstimated = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        uint16_t at = (uint16_t)(base + i);
        if (c == ' ') {
            steps.push_back({false, (uint32_t)(4 * dot), at, -1});      // 3 after the letter + 4 = 7
            continue;
        }
        const char* p = morse::encode(c);
        if (!*p) continue;
        for (int e = 0; p[e]; e++) {
            steps.push_back({true, (uint32_t)((p[e] == '-' ? 3 : 1) * dot), at, (int8_t)e});
            steps.push_back({false, (uint32_t)(p[e + 1] ? dot : 3 * dot), at, (int8_t)e});
        }
    }
}

TxView txView() {
    TxView v;
    v.text = txText;
    v.active = txActive;
    v.estimated = txEstimated;
    if (txDoneMs) v.doneMsAgo = millis() - txDoneMs;
    if (!txActive) return v;
    if (txEstimated) {
        if (!morse::timeline(txText, (uint32_t)morse::dotMs(wpm()), millis() - txStartMs, v.letter, v.element, v.down)) {
            v.letter = txText.size();
            v.element = -1;
        }
    } else {
        v.letter = txLetter;
        v.element = txElement;
        v.down = txDown;
    }
    return v;
}

std::string paddlePattern(bool* down) {
    if (down) *down = false;
    if (!pad.working) return "";
    if (pad.mode == KeyMode::STRAIGHT) {
        if (down) *down = straight.sounding();
        return straight.live(millis());
    }
    if (down) *down = iambic.sounding();
    return iambic.pattern();
}

void stop() {
    steps.clear();
    stepping = false;
    radioBusyUntil = 0;
    txActive = false;                   // cancelled: no "sent"
    txDoneMs = 0;
    key(false);
    M5Cardputer.Speaker.stop();
}

bool busy() { return stepping || !steps.empty() || (radioBusyUntil && (int32_t)(radioBusyUntil - millis()) > 0); }

void tick() {
    uint32_t now = millis();
    if (stepping && (int32_t)(now - stepEnd) >= 0) stepping = false;
    if (!stepping && !steps.empty()) {
        Step s = steps.front();
        steps.pop_front();
        key(s.down);
        if (s.down && groveKey() && !practice) onAirFor(s.ms + 1500);
        txLetter = s.letter;
        txElement = s.element;
        txDown = s.down;
        if (s.down) M5Cardputer.Speaker.tone((float)tone(), s.ms);
        stepEnd = now + s.ms;
        stepping = true;
        if (steps.empty() && !s.down) {}        // the last gap still counts as busy
    } else if (!stepping && keyIsDown) {
        key(false);
    }
    // The text has gone out: "sent" in the TX display.
    if (txActive && !busy()) {
        txActive = false;
        txDown = false;
        txLetter = txText.size();
        txDoneMs = now | 1;
    }

    if (pad.working) {
        // A cap plugged in later: its pins are not ours.
        static uint32_t lastCheck = 0;
        if (now - lastCheck > 1000) {
            lastCheck = now;
            if ((capPin(pad.dit) || capPin(pad.dah)) && capAttached()) {
                setupPaddle();
                if (!pad.working) {
                    M5Cardputer.Speaker.stop();
                    return;
                }
            }
        }
        bool a = readPin(pad.dit), b = pad.dah >= 0 && readPin(pad.dah);
        if (pad.swap && pad.dah >= 0) std::swap(a, b);
        int w = practicing ? practiceWpm : wpm();
        bool down;
        std::string got;
        if (pad.mode == KeyMode::STRAIGHT) {
            down = straight.update(now, a);
            got = straight.take();
        } else {
            iambic.setWpm(w);
            iambic.setSimple(pad.mode == KeyMode::SIMPLE);
            down = iambic.update(now, a, b);
            got = iambic.take();
        }
        if (down != paddleDown || (down && now - paddleToneMs > 1800)) {
            if (down) {
                M5Cardputer.Speaker.tone((float)tone(), 2000);
                paddleToneMs = now;
            } else {
                M5Cardputer.Speaker.stop();
            }
            // In the CW keyer the paddle keys the radio on the Grove key line.
            if (paddleTransmits() && groveKey() && steps.empty() && !stepping) digitalWrite(cap::GROVE_1, down ? HIGH : LOW);
            paddleDown = down;
        }
        if (down && paddleTransmits() && groveKey()) onAirFor(1500);
        if (!got.empty()) {
            paddleLetters += got;
            if (paddleLetters.size() > 60) paddleLetters.erase(0, paddleLetters.size() - 60);
            paddleNew += got;
            if (paddleNew.size() > 60) paddleNew.erase(0, paddleNew.size() - 60);
            // With CAT CW the radio sends what was keyed, a word at a time.
            if (paddleTransmits() && !groveKey() && rig::cwAvailable() && rig::connected()) {
                pendingWord += got;
                if (pendingWord.back() == ' ') {
                    rig::sendCw(pendingWord);
                    onAirFor(sendMs(pendingWord, wpm()) + 1000);
                    pendingWord.clear();
                }
            }
        }
    }
}

std::string paddleText() { return paddleLetters; }
void clearPaddleText() { paddleLetters.clear(); }
std::string takePaddleLetters() {
    std::string t;
    t.swap(paddleNew);
    return t;
}

std::string memory(int n) {
    static const char* defaults[6] = {
            "CQ CQ CQ DE {MYCALL} {MYCALL} K", "{CALL} DE {MYCALL} UR {RST} {RST} BK", "{CALL} TU 73 DE {MYCALL} SK",
            "QRZ? DE {MYCALL} K", "{CALL} 5NN {NR}", "CQ POTA DE {MYCALL} {MYCALL} {PARK} K"};
    if (n < 1 || n > 6) return "";
    return config::get(("cw_m" + std::to_string(n)).c_str(), defaults[n - 1]);
}

std::string expand(const std::string& text, const std::string& call, const std::string& nr) {
    auto rep = [](std::string s, const std::string& from, const std::string& to) {
        size_t p;
        while ((p = s.find(from)) != std::string::npos) s.replace(p, from.size(), to);
        return s;
    };
    std::string s = text;
    s = rep(s, "{MYCALL}", config::get("callsign"));
    s = rep(s, "{CALL}", call);
    s = rep(s, "{RST}", "5NN");
    s = rep(s, "{NR}", nr);
    s = rep(s, "{PARK}", config::get("pota_ref"));
    s = rep(s, "{SUMMIT}", config::get("sota_ref"));
    s = rep(s, "{NAME}", config::get("op_name"));
    std::string out;
    for (size_t i = 0; i < s.size(); i++)           // tidy double spaces left by empty fields
        if (!(s[i] == ' ' && (out.empty() || out.back() == ' '))) out += s[i];
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}  // namespace keyer
