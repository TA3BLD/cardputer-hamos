#include "Keyer.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <cstring>
#include <deque>
#include "Cap.h"
#include "Config.h"
#include "Rig.h"
#include "lib/Iambic.h"

namespace keyer {

namespace {

struct Step {
    bool down;
    uint32_t ms;
};
std::deque<Step> steps;             // the text being sent, as key down / up times
uint32_t stepEnd = 0;
bool keyIsDown = false, stepping = false;
uint32_t radioBusyUntil = 0;
int speed = 0;
morse::Iambic iambic;
bool paddleDown = false;
std::string paddleLetters, pendingWord;

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

bool paddle() { return config::get("grove_io") == "paddle"; }

void send(const std::string& textIn, bool sidetoneOnly) {
    std::string text;
    for (char c : textIn) text += (char)toupper((unsigned char)c);
    if (text.empty()) return;
    practice = sidetoneOnly;
    int dot = morse::dotMs(wpm());
    if (!sidetoneOnly && !groveKey() && rig::cwAvailable() && rig::connected()) {
        rig::sendCw(text);
        // about 50 dots per word of 5 letters
        radioBusyUntil = millis() + (uint32_t)(text.size() * 10 * dot);
        return;
    }
    for (char c : text) {
        if (c == ' ') {
            steps.push_back({false, (uint32_t)(4 * dot)});      // 3 after the letter + 4 = 7
            continue;
        }
        const char* p = morse::encode(c);
        if (!*p) continue;
        for (const char* e = p; *e; e++) {
            steps.push_back({true, (uint32_t)((*e == '-' ? 3 : 1) * dot)});
            steps.push_back({false, (uint32_t)(e[1] ? dot : 3 * dot)});
        }
    }
}

void stop() {
    steps.clear();
    stepping = false;
    radioBusyUntil = 0;
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
        if (s.down) M5Cardputer.Speaker.tone((float)tone(), s.ms);
        stepEnd = now + s.ms;
        stepping = true;
        if (steps.empty() && !s.down) {}        // the last gap still counts as busy
    } else if (!stepping && keyIsDown) {
        key(false);
    }

    if (paddle()) {
        iambic.setWpm(wpm());
        bool down = iambic.update(now, digitalRead(cap::GROVE_1) == LOW, digitalRead(cap::GROVE_2) == LOW);
        if (down != paddleDown) {
            paddleDown = down;
            if (down) M5Cardputer.Speaker.tone((float)tone(), 2000);
            else M5Cardputer.Speaker.stop();
        }
        std::string got = iambic.take();
        if (!got.empty()) {
            paddleLetters += got;
            if (paddleLetters.size() > 60) paddleLetters.erase(0, paddleLetters.size() - 60);
            // With CAT CW the radio sends what was keyed, a word at a time.
            if (rig::cwAvailable() && rig::connected()) {
                pendingWord += got;
                if (pendingWord.back() == ' ') {
                    rig::sendCw(pendingWord);
                    pendingWord.clear();
                }
            }
        }
    }
}

std::string paddleText() { return paddleLetters; }
void clearPaddleText() { paddleLetters.clear(); }

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
