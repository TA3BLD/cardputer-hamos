#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Openings.h"
#include "core/Rig.h"
#include "core/Ui.h"
#include "core/Watch.h"

// Alerts: the watch list (calls, prefixes VK9*, parks TA-*, NEW for a new
// country) with what was found, and band openings (TAB).
// E edits the list; ENTER on a hit tunes the radio; B switches the band alert.
class WatchApp : public App {
public:
    const char* name() const override { return T("Alerts"); }
    uint16_t color() const override { return 0xFD20; }

    void onEnter() override { editing_ = false; }

    void onTick(uint32_t) override {
        if (pending_ && !dirty) {
            pending_ = false;
            error_.clear();
            openings::check(error_);
            dirty = true;
        }
    }

    bool onBack() override {
        if (editing_) { editing_ = false; dirty = true; return true; }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (editing_) {
            if (k.enter) {
                config::set("watch", input_.value);
                config::save();
                editing_ = false;
            } else {
                input_.handle(k);
            }
            return;
        }
        if (k.tab) { page_ = 1 - page_; return; }
        if (page_ == 0) {
            if (k.ch == 'e' || k.ch == 'E') {
                input_ = ui::TextInput();
                input_.upper = true;
                input_.maxLen = 80;
                input_.value = config::get("watch");
                editing_ = true;
            } else if (k.enter && !watch::hits().empty()) {
                const watch::Hit& h = watch::hits()[watch::hits().size() - 1 - selected_];
                rig::tune(h.khz / 1000, "");
            } else {
                ui::listNav(k, selected_, (int)watch::hits().size());
            }
        } else {
            if (k.ch == 'r' || k.ch == 'R') pending_ = true;
            if (k.ch == 'b' || k.ch == 'B') {
                config::set("band_alert", config::get("band_alert", "0") == "1" ? "0" : "1");
                config::save();
            }
        }
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        int top = ui::BODY_Y + 13;
        if (page_ == 0) {
            std::string list = config::get("watch");
            ui::text(4, ui::BODY_Y + 2, std::string(T("Watch")) + ": " + (list.empty() ? T("(empty)") : list.substr(0, 30)),
                     ui::C_NOTICE);
            if (editing_) {
                ui::text(4, top + 4, T("Calls, prefixes (VK9*), parks (TA-*), NEW:"), ui::C_DIM);
                ui::text(4, top + 18, input_.value + "_", ui::C_TEXT);
                ui::footer(T("ENTER save   ` cancel"));
                return;
            }
            const auto& h = watch::hits();
            if (h.empty()) {
                ui::textCentered(top + 30, T("Nothing found yet"), ui::C_DIM);
            } else {
                std::vector<std::string> rows;
                for (size_t i = h.size(); i-- > 0;) {
                    char b[80];
                    snprintf(b, sizeof(b), "%-9.9s %8.1f %-7.7s %s", h[i].call.c_str(), h[i].khz, h[i].where.c_str(), h[i].info.c_str());
                    rows.push_back(b);
                }
                ui::list(rows, selected_, scroll_, nullptr, top, ui::ROWS - 1);
            }
            ui::footer(T("E edit list  ENTER tune  TAB openings"));
        } else {
            bool on = config::get("band_alert", "0") == "1";
            ui::text(4, ui::BODY_Y + 2, std::string(T("Band openings")) + (on ? " (" + std::string(T("alert on")) + ")" : ""), ui::C_NOTICE);
            if (pending_) {
                ui::textCentered(top + 30, T("Asking PSK Reporter..."), ui::C_DIM);
            } else if (!error_.empty()) {
                ui::textCentered(top + 30, error_, ui::C_WARN);
            } else if (openings::bands().empty()) {
                ui::textCentered(top + 30, openings::lastCheckMs() ? T("No long-distance paths now") : T("R checks now (WiFi)"), ui::C_DIM);
            } else {
                int y = top + 2;
                for (const openings::Band& b : openings::bands()) {
                    std::string c;
                    for (const std::string& x : b.continents) c += x + " ";
                    char line[80];
                    snprintf(line, sizeof(line), "%-5s %3d  %-15s %5.0f km", b.name.c_str(), b.paths, c.c_str(), b.farthestKm);
                    ui::text(4, y, line, ui::C_OK);
                    y += 12;
                    if (y > ui::H - ui::FOOTER_H - 10) break;
                }
            }
            ui::footer(T("R check  B alert on/off  TAB watch list"));
        }
    }

private:
    int page_ = 0, selected_ = 0, scroll_ = 0;
    bool editing_ = false, pending_ = false;
    ui::TextInput input_;
    std::string error_;
};
