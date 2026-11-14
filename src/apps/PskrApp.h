#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Net.h"
#include "core/PskReporter.h"
#include "core/Ui.h"

// "Who heard me": stations that reported our signal to PSK Reporter in the
// last hour, with distance and direction. Also shows our own reporting.
class PskrApp : public App {
public:
    const char* name() const override { return "PSK Reporter"; }
    uint16_t color() const override { return 0xFB56; }

    void onEnter() override {
        if (heard_.empty() && net::connected()) pending_ = true;
    }

    void onTick(uint32_t) override {
        if (pending_ && !dirty) {
            pending_ = false;
            error_.clear();
            if (!pskr::heardMe(heard_, error_)) heard_.clear();
            selected_ = scroll_ = 0;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (ui::listNav(k, selected_, (int)heard_.size())) { dirty = true; return; }
        if (k.ch == 'r' || k.ch == 'R') { pending_ = true; dirty = true; }
        if (k.ch == 'p' || k.ch == 'P') {
            bool on = config::get("psk_report", "0") == "1";
            config::set("psk_report", on ? "0" : "1");
            config::save();
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        int far = 0;
        for (const pskr::Heard& h : heard_) far = h.km > far ? (int)h.km : far;
        char head[64];
        snprintf(head, sizeof(head), "%s: %u  %s %d km", T("Heard by"), (unsigned)heard_.size(), T("max"), far);
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, head, ui::C_NOTICE);
        int top = ui::BODY_Y + 13;
        if (pending_) {
            ui::textCentered(60, T("Asking PSK Reporter..."), ui::C_DIM);
        } else if (heard_.empty()) {
            std::string msg = !error_.empty() ? error_ : net::connected() ? T("Nobody reported you in the last hour") : T("No WiFi");
            int y = 45;
            for (const std::string& l : ui::wrapLines(msg, 36)) { ui::textCentered(y, l, ui::C_DIM); y += 12; }
        } else {
            std::vector<std::string> rows;
            for (const pskr::Heard& h : heard_) {
                char line[80];
                char dist[16] = "";
                if (h.km >= 0) snprintf(dist, sizeof(dist), "%5.0fkm %03.0f", h.km, h.bearing);
                snprintf(line, sizeof(line), "%-9.9s %+3d %-5.5s %s %.3f", h.call.c_str(), h.snr, h.mode.c_str(), dist, h.freqHz / 1e6);
                rows.push_back(line);
            }
            ui::list(rows, selected_, scroll_, nullptr, top, ui::ROWS - 2);
            if (selected_ < (int)heard_.size()) {
                const pskr::Heard& h = heard_[selected_];
                ui::text(4, ui::H - ui::FOOTER_H - 11, h.locator + "  " + h.country, ui::C_DIM);
            }
        }
        std::string foot = config::get("psk_report", "0") == "1"
                                   ? std::string(T("Reporting on")) + " (" + std::to_string(pskr::sentTotal()) + ")  P off  R"
                                   : std::string(T("P report my decodes  R refresh"));
        ui::footer(foot.c_str());
    }

private:
    std::vector<pskr::Heard> heard_;
    std::string error_;
    int selected_ = 0, scroll_ = 0;
    bool pending_ = false;
};
