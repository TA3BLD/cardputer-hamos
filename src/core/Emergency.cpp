#include "Emergency.h"
#include <Arduino.h>
#include <algorithm>
#include <M5Cardputer.h>
#include "AprsIs.h"
#include "Config.h"
#include "Lang.h"
#include "LoraAprs.h"
#include "Mesh.h"
#include "Net.h"
#include "Station.h"
#include "lib/Aprs.h"
#include "lib/Emergency.h"

namespace emergency {

namespace {

State st = State::Idle;
uint32_t armedMs = 0, startMs = 0;
int sent = 0;
std::string note, last, via;
std::vector<uint32_t> myPacketIds;
std::vector<Alert> list;
bool newDistress = false;
uint32_t meshSeen = 0, aprsSeen = 0;

int timerMin = 0;
uint32_t lastKeyMs = 0;
bool warnedBeep = false;

bool sirenOn = false, strobeOn = false, saverOn = false;
uint32_t sirenNextMs = 0, strobeStartMs = 0;
bool lightOn = true;

constexpr uint32_t kCountdownMs = 5000;

int battery() { return M5Cardputer.Power.getBatteryLevel(); }

// Sends one text everywhere it can go; returns where it went.
std::string broadcast(const std::string& text, bool distress) {
    std::string where;
    std::string err;
    uint32_t id = 0;
    if (mesh::ready() && mesh::sendTextTo(text, mesh::BROADCAST, err, &id)) {
        where = "mesh";
        if (distress) myPacketIds.push_back(id);
    }
    double lat, lon;
    bool havePos = station::position(lat, lon);
    std::string call = aprsis::myCall();
    if (!call.empty() && havePos) {
        std::string comment = text.size() > 60 ? text.substr(0, 60) : text;
        // Emergency symbol: alternate table '!' ; ordinary status: a person '/['
        std::string info = aprs::formatPosition(lat, lon, distress ? '\\' : '/', distress ? '!' : '[', " " + comment);
        if (loraaprs::ready() && loraaprs::send(call + ">" + aprs::kToCall + ",WIDE1-1,WIDE2-1:" + info))
            where += where.empty() ? "LoRa-APRS" : ", LoRa-APRS";
        if (aprsis::online() && aprsis::sendRaw(aprs::header(call) + info)) where += where.empty() ? "APRS-IS" : ", APRS-IS";
        // Emergency contacts (APRS callsigns) get a message too.
        std::string contacts = config::get("sos_contacts");
        size_t a = 0;
        while (distress && a < contacts.size()) {
            size_t b = contacts.find(',', a);
            std::string to = contacts.substr(a, b == std::string::npos ? std::string::npos : b - a);
            while (!to.empty() && to[0] == ' ') to.erase(0, 1);
            std::string e;
            if (!to.empty()) aprsis::sendMessage(to, text.substr(0, 67), e);
            if (b == std::string::npos) break;
            a = b + 1;
        }
    }
    return where;
}

void sendSos() {
    double lat = 0, lon = 0;
    bool havePos = station::position(lat, lon);
    last = sos::text(config::get("callsign"), havePos, lat, lon, station::grid(), battery(), sent + 1, note);
    std::string where = broadcast(last, true);
    via = where.empty() ? T("Nothing could send: no mesh, LoRa-APRS or internet") : where;
    sent++;
}

void checkIncoming() {
    uint32_t added = mesh::messagesAdded();
    const std::vector<mesh::Message>& m = mesh::messages();
    size_t fresh = added - meshSeen < m.size() ? added - meshSeen : m.size();
    for (size_t i = m.size() - fresh; i < m.size(); i++) {
        if (m[i].mine) continue;
        bool distress = sos::isDistress(m[i].text);
        if (!distress && st != State::Active) continue;
        Alert a{mesh::nodeName(m[i].from), m[i].text, "mesh", millis(), !distress};
        list.push_back(a);
        if (distress) newDistress = true;
    }
    meshSeen = added;
    uint32_t aAdded = aprsis::messagesAdded();
    const std::vector<aprsis::Message>& am = aprsis::messages();
    size_t aFresh = aAdded - aprsSeen < am.size() ? aAdded - aprsSeen : am.size();
    for (size_t i = am.size() - aFresh; i < am.size(); i++) {
        if (am[i].mine) continue;
        bool distress = sos::isDistress(am[i].text);
        if (!distress && st != State::Active) continue;
        list.push_back({am[i].from, am[i].text, "APRS", millis(), !distress});
        if (distress) newDistress = true;
    }
    aprsSeen = aAdded;
    while (list.size() > 30) list.erase(list.begin());
}

// "... --- ..." on the speaker, loud and high.
void sirenTick(uint32_t now) {
    if (!sirenOn || (int32_t)(now - sirenNextMs) < 0) return;
    static const char* pattern = "...---...";
    static int pos = 0;
    static uint32_t gap = 0;
    if (pos >= 9) {
        pos = 0;
        sirenNextMs = now + 2500;
        return;
    }
    bool dah = pattern[pos++] == '-';
    uint32_t len = dah ? 450 : 150;
    M5Cardputer.Speaker.tone(2600, len);
    gap = 150;
    sirenNextMs = now + len + gap + (pos % 3 == 0 ? 300 : 0);
}

void strobeTick(uint32_t now) {
    if (!strobeOn) return;
    // the same rhythm as the siren: three short, three long, three short
    static const uint16_t steps[] = {200, 200, 200, 200, 200, 600, 600, 200, 600, 200, 600, 600, 200, 200, 200, 200, 200, 2000};
    uint32_t total = 0;
    for (uint16_t s : steps) total += s;
    uint32_t t = (now - strobeStartMs) % total;
    bool on = true;
    for (int i = 0; i < 18; i++) {
        if (t < steps[i]) { on = i % 2 == 0; break; }
        t -= steps[i];
    }
    if (on != lightOn) {
        lightOn = on;
        M5Cardputer.Display.setBrightness(on ? 255 : 0);
    }
}

}  // namespace

void begin() {
    meshSeen = mesh::messagesAdded();
    aprsSeen = aprsis::messagesAdded();
    lastKeyMs = millis();
}

void keyPressed() {
    lastKeyMs = millis();
    warnedBeep = false;
    if (st == State::Countdown) cancelCountdown();
}

void arm(const std::string& n) {
    if (st != State::Idle) return;
    note = n;
    st = State::Countdown;
    armedMs = millis();
}

void cancelCountdown() {
    if (st == State::Countdown) st = State::Idle;
}

void stop() {
    if (st != State::Active) return;
    st = State::Idle;
    std::string call = config::get("callsign");
    broadcast(std::string(T("All clear: I am OK")) + " - " + (call.empty() ? "HamOS" : call), false);
    sirenOn = false;
}

State state() { return st; }
int countdownSeconds() { return st == State::Countdown ? (int)((kCountdownMs - (millis() - armedMs) + 999) / 1000) : 0; }
int sentCount() { return sent; }
std::string lastText() { return last; }
std::string sentVia() { return via; }

uint32_t nextSendInMs() {
    if (st != State::Active) return 0;
    uint32_t due = sos::sendTime(sent) * 1000, el = millis() - startMs;
    return due > el ? due - el : 0;
}

bool repeatedByMesh() {
    for (const mesh::Message& m : mesh::messages())
        if (m.mine && m.relayed)
            for (uint32_t id : myPacketIds)
                if (id == m.packetId) return true;
    return false;
}

int presetCount() { return 6; }
const char* preset(int i) {
    static const char* p[] = {"OK - I am safe", "Need support, not urgent", "EMERGENCY: need medical help", "Need water / food",
                              "EMERGENCY: trapped, need rescue", "Moving to a shelter"};
    return i >= 0 && i < 6 ? T(p[i]) : "";
}

bool sendPreset(int i, std::string& where) {
    if (i < 0 || i >= presetCount()) return false;
    std::string call = config::get("callsign");
    std::string text = std::string(preset(i)) + " - " + (call.empty() ? "HamOS" : call) + " " + station::grid();
    where = broadcast(text, i == 2 || i == 4);     // medical help and trapped are distress calls
    return !where.empty();
}

const std::vector<Alert>& alerts() { return list; }
bool takeNewDistress() {
    bool n = newDistress;
    newDistress = false;
    return n;
}

void setTimer(int minutes) {
    timerMin = minutes < 0 ? 0 : minutes;
    lastKeyMs = millis();
    warnedBeep = false;
}
int timerMinutes() { return timerMin; }
int timerSecondsLeft() {
    if (!timerMin) return -1;
    long left = (long)timerMin * 60 - (long)((millis() - lastKeyMs) / 1000);
    return left < 0 ? 0 : (int)left;
}
bool timerWarning() { return timerMin && timerSecondsLeft() <= 60; }

void setSiren(bool on) {
    sirenOn = on;
    sirenNextMs = millis();
}
bool siren() { return sirenOn; }

void setStrobe(bool on) {
    strobeOn = on;
    strobeStartMs = millis();
    if (!on) {
        lightOn = true;
        M5Cardputer.Display.setBrightness((uint8_t)atoi(config::get("brightness", "160").c_str()));
    }
}
bool strobe() { return strobeOn; }
bool ownsBacklight() { return strobeOn; }

void setSaver(bool on) {
    saverOn = on;
    if (on) {
        net::off();
        M5Cardputer.Display.setBrightness(20);
    } else {
        net::begin();
        M5Cardputer.Display.setBrightness((uint8_t)atoi(config::get("brightness", "160").c_str()));
    }
}
bool saver() { return saverOn; }

void tick() {
    uint32_t now = millis();
    checkIncoming();
    if (st == State::Countdown && now - armedMs >= kCountdownMs) {
        st = State::Active;
        startMs = now;
        sent = 0;
        myPacketIds.clear();
        list.erase(std::remove_if(list.begin(), list.end(), [](const Alert& a) { return a.reply; }), list.end());
        sendSos();
    }
    if (st == State::Active && now - startMs >= sos::sendTime(sent) * 1000) sendSos();

    if (timerMin && st == State::Idle) {
        int left = timerSecondsLeft();
        if (left <= 60 && left > 0 && (now / 1000) % 4 == 0 && !warnedBeep) {     // loud reminder every few seconds
            M5Cardputer.Speaker.tone(3000, 400);
            warnedBeep = true;
        } else if ((now / 1000) % 4 != 0) {
            warnedBeep = false;
        }
        if (left == 0) {
            note = T("no answer to safety timer");
            st = State::Active;
            startMs = now;
            sent = 0;
            myPacketIds.clear();
            sendSos();
            timerMin = 0;
            sirenOn = true;
        }
    }
    sirenTick(now);
    strobeTick(now);
}

}  // namespace emergency
