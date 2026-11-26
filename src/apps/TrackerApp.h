#pragma once
#include <M5Cardputer.h>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/Lang.h"
#include "core/Mailbox.h"
#include "core/Mesh.h"
#include "core/Tracker.h"
#include "core/Ui.h"

// Tracker (SmartBeaconing position beacons, S starts / stops, D dark screen)
// and the mesh mailbox (TAB): mail kept for nodes that are away.
class TrackerApp : public App {
public:
    const char* name() const override { return T("Tracker"); }
    uint16_t color() const override { return 0x3D8E; }

    void onTick(uint32_t now) override {
        if (now - drawMs_ > 1000) { drawMs_ = now; dirty = true; }
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (dark_) {                            // any key lights the screen again
            dark_ = false;
            M5Cardputer.Display.setBrightness((uint8_t)atoi(config::get("brightness", "160").c_str()));
            return;
        }
        if (k.tab) { page_ = 1 - page_; return; }
        if (page_ == 0) {
            if (k.ch == 's' || k.ch == 'S') tracker::running() ? tracker::stop() : tracker::start();
            if ((k.ch == 'd' || k.ch == 'D') && tracker::running()) {
                dark_ = true;
                M5Cardputer.Display.setBrightness(0);
            }
        } else {
            if (ui::listNav(k, selected_, (int)mailbox::waiting().size())) return;
            if (k.del && !mailbox::waiting().empty()) mailbox::remove((size_t)selected_);
            if (k.ch == 'm' || k.ch == 'M') {
                config::set("mesh_mailbox", config::get("mesh_mailbox", "0") == "1" ? "0" : "1");
                config::save();
            }
        }
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        char b[80];
        if (page_ == 0) {
            ui::text(4, ui::BODY_Y + 2, tracker::running() ? T("Tracker on") : T("Tracker off"), tracker::running() ? ui::C_OK : ui::C_DIM);
            int y = ui::BODY_Y + 16;
            if (!gps::hasFix()) {
                ui::text(4, y, T("Waiting for a GPS fix..."), ui::C_WARN);
            } else {
                snprintf(b, sizeof(b), "%.0f km/h  %03.0f deg", gps::fix().speedKmh, gps::fix().courseDeg);
                ui::text(4, y, b, ui::C_TEXT, 1.5);
            }
            y += 20;
            snprintf(b, sizeof(b), "%s: %u   %s", T("Beacons"), (unsigned)tracker::sent(), tracker::via().c_str());
            ui::text(4, y, b, ui::C_TEXT);
            y += 12;
            double next = tracker::secondsToNext();
            if (next >= 0) {
                snprintf(b, sizeof(b), "%s ~%.0f s", T("next in"), next);
                ui::text(4, y, b, ui::C_DIM);
            }
            y += 14;
            for (const std::string& l : ui::wrapLines(T("Beacons often when fast, rarely when slow, and at once on a corner."), 38)) {
                ui::text(4, y, l, ui::C_DIM);
                y += 10;
            }
            ui::footer(T("S on/off  D dark  TAB mailbox"));
        } else {
            bool on = config::get("mesh_mailbox", "0") == "1";
            snprintf(b, sizeof(b), "%s %s  %u %s", T("Mailbox"), on ? T("on") : T("off"), (unsigned)mailbox::delivered(), T("delivered"));
            ui::text(4, ui::BODY_Y + 2, b, on ? ui::C_NOTICE : ui::C_DIM);
            const auto& w = mailbox::waiting();
            if (w.empty()) {
                int y = ui::BODY_Y + 24;
                for (const std::string& l : ui::wrapLines(T("\"@NAME text\" on the channel for a node that is away is kept here and sent to it when it is heard again."), 38)) {
                    ui::text(4, y, l, ui::C_DIM);
                    y += 10;
                }
            } else {
                std::vector<std::string> rows;
                for (const mailbox::Mail& m : w) rows.push_back(mesh::nodeName(m.to, true) + " < " + mesh::nodeName(m.from, true) + ": " + m.text);
                ui::list(rows, selected_, scroll_, nullptr, ui::BODY_Y + 13, ui::ROWS - 1);
            }
            ui::footer(T("M on/off  DEL delete  TAB tracker"));
        }
    }

private:
    int page_ = 0, selected_ = 0, scroll_ = 0;
    bool dark_ = false;
    uint32_t drawMs_ = 0;
};
