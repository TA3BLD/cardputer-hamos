#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Config.h"
#include "core/Flags.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Ui.h"
#include "core/Watch.h"
#include "lib/Adif.h"
#include "lib/DxSpot.h"
#include "lib/Dxcc.h"

// Live DX cluster spots over telnet. Countries you have never logged are
// highlighted and announced with a beep.
class DxClusterApp : public App {
public:
    const char* name() const override { return T("DX Cluster"); }
    uint16_t color() const override { return 0xF81F; }

    void onEnter() override {
        logbook::load();
        spots_.clear();
        selected_ = scroll_ = 0;
        line_.clear();
        loggedIn_ = false;
        if (config::get("callsign").empty()) { state_ = IDLE; status_ = "Set your callsign in Settings first"; }
        else if (!net::connected()) { state_ = IDLE; status_ = T("No WiFi connection"); }
        else { state_ = CONNECT_PENDING; status_ = "Connecting to " + config::get("dx_host", kDefaultHost) + "..."; }
    }

    void onExit() override { client_.stop(); }

    void onTick(uint32_t now) override {
        if (state_ == CONNECT_NOW) {            // the "Connecting" screen has been drawn
            int port = atoi(config::get("dx_port", "23").c_str());
            if (client_.connect(config::get("dx_host", kDefaultHost).c_str(), (uint16_t)port, 6000)) {
                state_ = ONLINE;
                connectedMs_ = now;
                status_ = T("Connected, waiting for spots...");
            } else {
                state_ = IDLE;
                status_ = T("Could not reach the cluster");
            }
            dirty = true;
            return;
        }
        if (state_ != ONLINE) return;
        if (!client_.connected()) {
            state_ = IDLE;
            status_ = T("Connection closed by the cluster");
            dirty = true;
            return;
        }
        if (!loggedIn_ && now - connectedMs_ > 1500) login();
        for (int budget = 512; budget > 0 && client_.available(); budget--) {
            char c = (char)client_.read();
            if (c == '\n') {
                handleLine(line_);
                line_.clear();
            } else if (c != '\r' && line_.size() < 200) {
                line_ += c;
                if (!loggedIn_ && (line_.find("ogin:") != std::string::npos || line_.find("call:") != std::string::npos)) login();
            }
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.left() || k.right()) {
            band_ = (band_ + (k.right() ? 1 : kBands - 1)) % kBands;
            selected_ = scroll_ = 0;
        } else if (!ui::listNav(k, selected_, (int)visible().size())) {
            return;
        }
        dirty = true;
    }

    void draw() override {
        if (state_ == CONNECT_PENDING) state_ = CONNECT_NOW;
        std::vector<const Row*> rows = visible();
        if (rows.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 40, status_, state_ == IDLE ? ui::C_WARN : ui::C_DIM);
        } else {
            std::vector<std::string> text;
            std::vector<uint16_t> colors;
            char line[64];
            for (const Row* r : rows) {
                snprintf(line, sizeof(line), "%8.1f %-10.10s    %-10.10s %.4s", r->spot.khz, r->spot.call.c_str(),
                         r->entity ? r->entity->name : "", r->spot.time.c_str());
                text.push_back(line);
                colors.push_back(r->isNew ? ui::C_WARN : ui::C_TEXT);
            }
            ui::list(text, selected_, scroll_, &colors);
            for (int i = 0; i < ui::ROWS && scroll_ + i < (int)rows.size(); i++)
                if (rows[(size_t)(scroll_ + i)]->entity)
                    flags::drawSmall(rows[(size_t)(scroll_ + i)]->entity->iso, 6 + 20 * 6, ui::BODY_Y + i * ui::ROW_H + 1);
        }
        std::string hint;
        if (!rows.empty()) {
            const Row* r = rows[(size_t)selected_];
            hint = (r->isNew ? T("NEW! ") : "") + r->spot.comment + " <" + r->spot.spotter + ">";
            if (hint.size() > 39) hint.resize(39);
        } else {
            hint = std::string(T("Band ")) + bandLabel(band_) + T("   , and / change band");
        }
        ui::footer(hint.c_str());
    }

private:
    enum State { IDLE, CONNECT_PENDING, CONNECT_NOW, ONLINE };
    struct Row {
        dxspot::Spot spot;
        const dxcc::Entity* entity;
        std::string band;
        bool isNew;
    };
    static constexpr const char* kDefaultHost = "dxc.ve7cc.net";
    static constexpr int kBands = 12;

    static const char* bandLabel(int i) {
        static const char* b[kBands] = {"ALL", "160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "2m"};
        return b[i];
    }

    void login() {
        client_.print((config::get("callsign") + "\r\n").c_str());
        loggedIn_ = true;
    }

    void handleLine(const std::string& line) {
        dxspot::Spot s;
        if (!dxspot::parse(line, s)) return;
        watch::check(s.call, s.khz, "", "cluster", s.comment);
        Row r;
        r.spot = s;
        r.entity = dxcc::lookup(s.call);
        r.band = adif::band(s.khz / 1000.0);
        r.isNew = r.entity && !logbook::workedEntity(r.entity);
        spots_.insert(spots_.begin(), r);
        if (spots_.size() > 40) spots_.pop_back();
        if (selected_ > 0) selected_++;         // keep the highlighted spot under the cursor
        bool shown = band_ == 0 || r.band == bandLabel(band_);
        if (r.isNew && shown && config::get("dx_beep", "1") == "1") M5Cardputer.Speaker.tone(2200, 80);
        dirty = true;
    }

    std::vector<const Row*> visible() {
        std::vector<const Row*> out;
        for (const Row& r : spots_)
            if (band_ == 0 || r.band == bandLabel(band_)) out.push_back(&r);
        if (selected_ >= (int)out.size()) selected_ = out.empty() ? 0 : (int)out.size() - 1;
        return out;
    }

    WiFiClient client_;
    State state_ = IDLE;
    std::vector<Row> spots_;
    std::string line_, status_;
    uint32_t connectedMs_ = 0;
    bool loggedIn_ = false;
    int band_ = 0, selected_ = 0, scroll_ = 0;
};
