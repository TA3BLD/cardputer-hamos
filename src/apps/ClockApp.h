#pragma once
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "core/App.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Ui.h"

// UTC clock (NTP-synced) with local time from the utc_offset setting.
class ClockApp : public App {
public:
    const char* name() const override { return "UTC Clock"; }
    uint16_t color() const override { return 0xFD20; }

    void onTick(uint32_t nowMs) override {
        if (nowMs - lastMs_ >= 1000) {
            lastMs_ = nowMs;
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        if (!net::timeValid()) {
            ui::textCentered(45, "No time yet", ui::C_WARN, 2);
            ui::textCentered(75, net::configured() ? "Waiting for WiFi / NTP..." : "Set WiFi in Settings", ui::C_DIM);
        } else {
            time_t now = time(nullptr);
            struct tm t;
            char buf[32];
            gmtime_r(&now, &t);
            strftime(buf, sizeof(buf), "%H:%M:%S", &t);
            ui::textCentered(24, buf, ui::C_TEXT, 4);
            strftime(buf, sizeof(buf), "%Y-%m-%d UTC", &t);
            ui::textCentered(64, buf, ui::C_DIM, 1.5);

            double offset = atof(config::get("utc_offset", "3").c_str());
            time_t local = now + (time_t)(offset * 3600);
            gmtime_r(&local, &t);
            char hhmm[8];
            strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
            snprintf(buf, sizeof(buf), "Local %s (UTC%+g)", hhmm, offset);
            ui::textCentered(90, buf, ui::C_OK, 1.5);
        }
        ui::footer("` back");
    }

private:
    uint32_t lastMs_ = 0;
};
