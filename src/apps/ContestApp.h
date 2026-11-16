#pragma once
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Ui.h"
#include "lib/Adif.h"

// Fast contest logging: type the callsign (and the number received), ENTER
// logs it with the frequency and mode from the radio (or the last ones used),
// RST 59/599 and our serial number counting up. Dupes show while typing.
// In CW with a keyer: the first ENTER sends the exchange, the second logs and
// sends TU. Fn+DEL removes the last contact.
class ContestApp : public App {
public:
    const char* name() const override { return T("Contest"); }
    uint16_t color() const override { return 0x07E0; }

    void onEnter() override {
        logbook::load();
        call_.upper = true;
        call_.maxLen = 12;
        nr_.maxLen = 8;
        nr_.upper = true;
        freq_.maxLen = 9;
        mode_.maxLen = 6;
        mode_.upper = true;
        if (freq_.value.empty()) freq_.value = config::get("contest_freq", "14.025");
        if (mode_.value.empty()) mode_.value = config::get("contest_mode", "CW");
        field_ = CALL;
        sentExchange_ = false;
    }

    void onTick(uint32_t now) override {
        if (rig::connected() && (rig::freqMhz() != freq_.value || (!rig::mode().empty() && rig::mode() != mode_.value && rig::mode() != "DATA"))) {
            freq_.value = rig::freqMhz();
            if (!rig::mode().empty() && rig::mode() != "DATA") mode_.value = rig::mode();
            dirty = true;
        }
        if (now - lastMs_ > 5000) { lastMs_ = now; dirty = true; }
    }

    bool onBack() override {
        if (!call_.value.empty() || !nr_.value.empty()) {
            call_.value.clear();
            nr_.value.clear();
            sentExchange_ = false;
            field_ = CALL;
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.del && k.fn) { undo(); return; }
        if (k.tab) {
            field_ = (Field)((field_ + 1) % FIELDS);
            return;
        }
        if (k.enter) {
            if (field_ == FREQ || field_ == MODE) {
                config::set("contest_freq", freq_.value);
                config::set("contest_mode", mode_.value);
                field_ = CALL;
                return;
            }
            if (call_.value.size() < 3) return;
            bool cw = mode_.value == "CW";
            if (cw && !sentExchange_ && keyer::outputName() != std::string("sidetone only")) {
                keyer::send(keyer::expand("{CALL} 5NN {NR}", call_.value, serialText()));
                sentExchange_ = true;
                field_ = NR;
                return;
            }
            log();
            if (cw && keyer::outputName() != std::string("sidetone only")) keyer::send(keyer::expand("TU {MYCALL}", "", ""));
            return;
        }
        ui::TextInput& in = field_ == CALL ? call_ : field_ == NR ? nr_ : field_ == FREQ ? freq_ : mode_;
        if (field_ == CALL && k.ch == ' ') { field_ = NR; return; }     // space jumps to the number
        in.handle(k);
    }

    void draw() override {
        ui::clearBody();
        char head[64];
        snprintf(head, sizeof(head), "%s %s  #%s  %d/h", freq_.value.c_str(), mode_.value.c_str(), serialText().c_str(), rate());
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, head, field_ >= FREQ ? ui::C_NOTICE : ui::C_OK);
        int y = ui::BODY_Y + 16;
        ui::text(4, y + 4, T("Call"), field_ == CALL ? ui::C_NOTICE : ui::C_DIM);
        ui::text(40, y, call_.value + (field_ == CALL ? "_" : ""), ui::C_TEXT, 2);
        ui::text(150, y + 4, "NR", field_ == NR ? ui::C_NOTICE : ui::C_DIM);
        ui::text(168, y, nr_.value + (field_ == NR ? "_" : ""), ui::C_TEXT, 2);
        y += 20;
        if (field_ == FREQ || field_ == MODE) {
            ui::text(4, y, std::string("MHz ") + freq_.value + (field_ == FREQ ? "_" : ""), ui::C_NOTICE);
            ui::text(120, y, std::string(T("Mode")) + " " + mode_.value + (field_ == MODE ? "_" : ""), ui::C_NOTICE);
        } else if (call_.value.size() >= 3) {
            int dupes = 0;
            int n = logbook::worked(call_.value, freq_.value, mode_.value, dupes);
            if (dupes) ui::text(4, y, T("DUPE!"), ui::C_ERR, 1.5);
            else if (n) ui::text(4, y, std::string(T("worked ")) + std::to_string(n) + T("x before"), ui::C_WARN);
            else ui::text(4, y, T("new"), ui::C_OK);
            if (sentExchange_) ui::text(120, y, T("exchange sent"), ui::C_DIM);
        }
        y += 14;
        const auto& r = logbook::recent();
        for (size_t i = r.size(), shown = 0; i-- > 0 && shown < 4; shown++) {
            const adif::Qso& q = r[i];
            char line[64];
            snprintf(line, sizeof(line), "%.4s %-10.10s %-7.7s %s %s", q.time.c_str(), q.call.c_str(), q.freq.c_str(), q.stx.c_str(), q.srx.c_str());
            ui::text(4, y, line, ui::C_DIM);
            y += 10;
        }
        ui::footer(!status_.empty() ? status_.c_str() : T("ENTER log  SPACE nr  TAB freq/mode  Fn+DEL undo"));
        status_.clear();
    }

private:
    enum Field { CALL, NR, FREQ, MODE, FIELDS };
    Field field_ = CALL;
    ui::TextInput call_, nr_, freq_, mode_;
    bool sentExchange_ = false;
    std::string status_;
    uint32_t lastMs_ = 0;

    static int highestSerial() {
        int h = 0;
        for (const adif::Qso& q : logbook::recent())
            if (atoi(q.stx.c_str()) > h) h = atoi(q.stx.c_str());
        return h;
    }
    static std::string serialText() {
        char b[16];
        snprintf(b, sizeof(b), "%03d", highestSerial() + 1);
        return b;
    }
    static int rate() {
        if (!net::timeValid()) return 0;
        long long now = net::utcMillis() / 1000;
        int n = 0;
        for (const adif::Qso& q : logbook::recent())
            if (now - adif::startUnix(q) < 3600) n++;
        return n;
    }

    void log() {
        adif::Qso q;
        q.call = call_.value;
        q.freq = freq_.value;
        q.mode = mode_.value;
        q.rstSent = q.rstRcvd = (q.mode == "CW" || q.mode == "RTTY") ? "599" : "59";
        q.stx = serialText();
        q.srx = nr_.value;
        q.potaRef = config::get("pota_ref");
        q.sotaRef = config::get("sota_ref");
        if (net::timeValid()) {
            time_t now = (time_t)(net::utcMillis() / 1000);
            struct tm t;
            gmtime_r(&now, &t);
            char b[16];
            strftime(b, sizeof(b), "%Y%m%d", &t);
            q.date = b;
            strftime(b, sizeof(b), "%H%M", &t);
            q.time = b;
        }
        status_ = logbook::add(q) ? std::string(T("Logged ")) + q.call : T("No SD card: lost at power-off");
        call_.value.clear();
        nr_.value.clear();
        sentExchange_ = false;
        field_ = CALL;
    }

    void undo() {
        const auto& r = logbook::recent();
        if (r.empty()) return;
        std::string call = r.back().call;
        status_ = logbook::remove(r.size() - 1) ? std::string(T("Deleted ")) + call : T("Delete failed");
    }
};
