#pragma once
#include <cstdio>
#include <string>
#include "core/App.h"
#include "core/Callbook.h"
#include "core/Flags.h"
#include "core/Lang.h"
#include "core/Log.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Dxcc.h"
#include "lib/Maidenhead.h"

// Looks up a callsign in HamQTH or QRZ.com: name, town, locator, distance.
class CallbookApp : public App {
public:
    const char* name() const override { return T("Callbook"); }
    uint16_t color() const override { return 0x867F; }

    void onEnter() override {
        logbook::load();
        input_.upper = true;
        input_.maxLen = 12;
    }

    void onTick(uint32_t) override {
        if (pending_ && !dirty) {
            pending_ = false;
            error_.clear();
            found_ = callbook::lookup(input_.value, info_, error_);
            dirty = true;
        }
    }

    bool onBack() override {
        if (!input_.value.empty()) {
            input_.value.clear();
            found_ = false;
            error_.clear();
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (k.enter && input_.value.size() >= 3) {
            pending_ = true;
            dirty = true;
            return;
        }
        if (input_.handle(k)) {
            found_ = false;
            error_.clear();
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        input_.draw(ui::BODY_Y + 2, T("Call"));
        int y = ui::BODY_Y + 18;
        if (!callbook::configured()) {
            for (const std::string& l : ui::wrapLines(T("Set a HamQTH (free) or QRZ.com user and password in Settings."), 38)) {
                ui::text(4, y, l, ui::C_DIM);
                y += 11;
            }
        } else if (pending_) {
            ui::text(4, y, std::string(T("Asking")) + " " + callbook::service() + "...", ui::C_DIM);
        } else if (found_) {
            const dxcc::Entity* e = dxcc::lookup(info_.call);
            int x = 4;
            if (e && flags::drawSmall(e->iso, 4, y + 1)) x = 26;
            ui::text(x, y, info_.call, ui::C_OK, 1.5);
            y += 18;
            if (!info_.name.empty()) { ui::text(4, y, info_.name); y += 12; }
            if (!info_.qth.empty() || !info_.country.empty()) {
                ui::text(4, y, info_.qth + (info_.qth.empty() || info_.country.empty() ? "" : ", ") + info_.country, ui::C_TEXT);
                y += 12;
            }
            double lat, lon, myLat, myLon;
            if (maidenhead::toLatLon(info_.grid, lat, lon) && station::position(myLat, myLon)) {
                char b[64];
                snprintf(b, sizeof(b), "%s  %.0f km  %03.0f deg", info_.grid.c_str(), maidenhead::distanceKm(myLat, myLon, lat, lon),
                         maidenhead::bearingDeg(myLat, myLon, lat, lon));
                ui::text(4, y, b, ui::C_NOTICE);
                y += 12;
            } else if (!info_.grid.empty()) {
                ui::text(4, y, info_.grid, ui::C_NOTICE);
                y += 12;
            }
            int dupes = 0;
            int n = logbook::worked(info_.call, "", "", dupes);
            ui::text(4, y, n ? std::string(T("In your log")) + ": " + std::to_string(n) : T("Not in your log yet"),
                     n ? ui::C_DIM : ui::C_WARN);
        } else if (!error_.empty()) {
            ui::text(4, y, error_, ui::C_ERR);
        }
        ui::footer(callbook::configured() ? T("Type a call, ENTER look up") : T("` back"));
    }

private:
    ui::TextInput input_;
    callbook::Info info_;
    std::string error_;
    bool found_ = false, pending_ = false;
};
