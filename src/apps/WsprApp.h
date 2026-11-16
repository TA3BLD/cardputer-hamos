#pragma once
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Http.h"
#include "core/Lang.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/JsonDom.h"
#include "lib/Wspr.h"

// WSPR beacon: sends "CALL GRID dBm" in two-minute slots (every N-th slot) at
// a random audio frequency between 1400 and 1600 Hz, with PTT. H shows who
// heard you in the last hour (wspr.live, the public copy of WSPRnet).
// T switches sending on/off.
class WsprApp : public App {
public:
    const char* name() const override { return "WSPR"; }
    uint16_t color() const override { return 0x7BEF; }

    void onExit() override {
        if (sending_) audio::stopPlaying();
        sending_ = false;
        on_ = false;
    }

    void onTick(uint32_t now) override {
        if (pending_ && !dirty) {
            pending_ = false;
            fetchHeard();
            dirty = true;
        }
        if (sending_ && !audio::playing()) sending_ = false;
        if (on_ && !sending_ && net::timeValid()) {
            long long t = net::utcMillis();
            long long slot = t / 120000;
            int ms = (int)(t % 120000);
            int every = atoi(config::get("wspr_every", "5").c_str());
            if (every < 1) every = 1;
            if (slot != lastSlot_ && ms < 1500 && ms >= 500) {
                lastSlot_ = slot;
                if (++slotCount_ % every == 1 || every == 1) start(ms);
            }
        }
        if (now - drawMs_ > 1000) { drawMs_ = now; dirty = true; }
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.ch == 't' || k.ch == 'T') {
            on_ = !on_;
            slotCount_ = 0;
            message_.clear();
            if (on_ && !check()) on_ = false;
            if (!on_ && sending_) {
                audio::stopPlaying();
                sending_ = false;
            }
        } else if (k.ch == 'h' || k.ch == 'H') {
            pending_ = true;
        } else {
            ui::listNav(k, selected_, (int)heard_.size());
        }
    }

    void draw() override {
        ui::clearBody();
        std::string call = config::get("callsign"), grid = station::grid().substr(0, 4);
        int dbm = wspr::validPower(atoi(config::get("wspr_dbm", "23").c_str()));
        char b[96];
        snprintf(b, sizeof(b), "%s %s %d dBm  %s MHz", call.c_str(), grid.c_str(), dbm, dial().c_str());
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, b, ui::C_NOTICE);
        int y = ui::BODY_Y + 15;
        if (sending_) {
            snprintf(b, sizeof(b), "TX %.0f Hz  %d%%", txHz_, (int)(synth_ ? synth_->progress() * 100 : 0));
            ui::text(4, y, b, ui::C_ERR, 1.5);
        } else if (on_ && net::timeValid()) {
            long long t = net::utcMillis();
            int left = (int)((120000 - t % 120000) / 1000);
            snprintf(b, sizeof(b), "%s %d:%02d  (1/%s)", T("next slot in"), left / 60, left % 60, config::get("wspr_every", "5").c_str());
            ui::text(4, y, b, ui::C_OK);
        } else {
            ui::text(4, y, message_.empty() ? T("Beacon off (T)") : message_, message_.empty() ? ui::C_DIM : ui::C_WARN);
        }
        y += 18;
        if (heard_.empty()) {
            ui::text(4, y, heardStatus_.empty() ? T("H: who heard me (wspr.live)") : heardStatus_, ui::C_DIM);
        } else {
            std::vector<std::string> rows;
            for (const Spot& s : heard_) {
                snprintf(b, sizeof(b), "%-8.8s %-6.6s %+3d %5dkm %s", s.call.c_str(), s.grid.c_str(), s.snr, s.km, s.time.c_str());
                rows.push_back(b);
            }
            ui::list(rows, selected_, scroll_, nullptr, y, ui::ROWS - 3);
        }
        ui::footer(T("T beacon on/off  H heard by"));
    }

private:
    struct Spot {
        std::string call, grid, time;
        int snr = 0, km = 0;
    };
    bool on_ = false, sending_ = false, pending_ = false;
    long long lastSlot_ = -1;
    int slotCount_ = 0, selected_ = 0, scroll_ = 0;
    float txHz_ = 1500;
    uint32_t drawMs_ = 0;
    std::string message_, heardStatus_;
    std::vector<Spot> heard_;
    std::unique_ptr<wspr::Synth> synth_;

    std::string dial() const { return rig::connected() ? rig::freqMhz() : config::get("wspr_dial", "14.0956"); }

    bool check() {
        uint8_t t[wspr::kSymbols];
        if (!wspr::tones(config::get("callsign"), station::grid(), atoi(config::get("wspr_dbm", "23").c_str()), t)) {
            message_ = T("Callsign or grid cannot be sent in WSPR");
            return false;
        }
        if (!net::timeValid()) {
            message_ = T("Needs the time: WiFi or GPS");
            return false;
        }
        return true;
    }

    void start(int ms) {
        uint8_t t[wspr::kSymbols];
        if (!wspr::tones(config::get("callsign"), station::grid(), atoi(config::get("wspr_dbm", "23").c_str()), t)) return;
        txHz_ = 1400.0f + (float)(esp_random() % 200);
        float lead = 1.0f - ms / 1000.0f;
        synth_.reset(new wspr::Synth(t, txHz_, 8000, 14000, lead > 0 ? lead : 0));
        audio::play([](int16_t* out, size_t max, void* self) { return ((WsprApp*)self)->synth_->read(out, max); }, this, 8000);
        audio::markTransmit();
        sending_ = true;
    }

    void fetchHeard() {
        heard_.clear();
        std::string call = config::get("callsign");
        if (call.empty()) { heardStatus_ = T("Set your callsign in Settings"); return; }
        std::string q = "SELECT time, rx_sign, rx_loc, distance, snr FROM wspr.rx WHERE tx_sign = '" + call +
                        "' AND time > now() - 3600 ORDER BY time DESC LIMIT 40 FORMAT JSON";
        http::Request r;
        r.url = "https://db1.wspr.live/?query=" + http::urlEncode(q);
        r.maxBytes = 32 * 1024;
        http::Response a = http::fetch(r);
        jdom::Value v;
        if (!a.ok() || !jdom::parse(a.body, v)) { heardStatus_ = a.ok() ? T("Unexpected answer from the server") : a.error; return; }
        const jdom::Value& rows = v["data"];
        for (size_t i = 0; i < rows.size(); i++) {
            Spot s;
            s.call = rows[i]["rx_sign"].str();
            s.grid = rows[i]["rx_loc"].str();
            std::string t = rows[i]["time"].str();
            s.time = t.size() >= 16 ? t.substr(11, 5) : t;
            s.snr = (int)rows[i]["snr"].num();
            s.km = (int)rows[i]["distance"].num();
            heard_.push_back(s);
        }
        heardStatus_ = heard_.empty() ? T("Nobody reported you in the last hour") : "";
    }
};
