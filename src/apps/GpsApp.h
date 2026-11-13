#pragma once
#include <cstdio>
#include "core/App.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/Ui.h"

// Live position from the GNSS receiver on the LoRa cap.
class GpsApp : public App {
public:
    const char* name() const override { return "GPS"; }
    uint16_t color() const override { return 0x07E0; }

    void onEnter() override { status_.clear(); }

    void onTick(uint32_t now) override {
        if (now - lastMs_ >= 1000) {
            lastMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.enter && gps::hasFix()) {
            config::set("grid", gps::grid());
            status_ = config::save() ? "Saved as home grid" : "No SD card: not saved";
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        const nmea::Fix& f = gps::fix();
        char line[48];
        if (!gps::present()) {
            ui::textCentered(ui::BODY_Y + 28, "No GPS data", ui::C_WARN, 2);
            ui::textCentered(ui::BODY_Y + 60, "Is the LoRa / GNSS cap attached?", ui::C_DIM);
            ui::footer("` back");
            return;
        }
        if (!gps::hasFix()) {
            ui::textCentered(ui::BODY_Y + 24, "Searching...", ui::C_WARN, 2);
            snprintf(line, sizeof(line), "%d satellites in use", f.satellites);
            ui::textCentered(ui::BODY_Y + 54, line, ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 70, "A first fix needs open sky, up to a few minutes", ui::C_DIM);
            ui::footer("` back");
            return;
        }
        ui::text(8, ui::BODY_Y + 6, gps::grid(), ui::C_OK, 3);
        snprintf(line, sizeof(line), "%d sats", f.satellites);
        ui::text(150, ui::BODY_Y + 8, line, ui::C_DIM);
        snprintf(line, sizeof(line), "%.0f m", f.altitudeM);
        ui::text(150, ui::BODY_Y + 20, line, ui::C_DIM);
        snprintf(line, sizeof(line), "%.5f %c", f.lat < 0 ? -f.lat : f.lat, f.lat < 0 ? 'S' : 'N');
        ui::text(8, ui::BODY_Y + 40, line, ui::C_TEXT, 1.5);
        snprintf(line, sizeof(line), "%.5f %c", f.lon < 0 ? -f.lon : f.lon, f.lon < 0 ? 'W' : 'E');
        ui::text(8, ui::BODY_Y + 56, line, ui::C_TEXT, 1.5);
        snprintf(line, sizeof(line), "%.1f km/h   %03.0f deg", f.speedKmh, f.courseDeg);
        ui::text(8, ui::BODY_Y + 76, line, ui::C_DIM);
        if (f.timeValid) {
            snprintf(line, sizeof(line), "GPS time %02d:%02d:%02d UTC", f.hour, f.minute, f.second);
            ui::text(8, ui::BODY_Y + 90, line, ui::C_DIM);
        }
        ui::footer(status_.empty() ? "ENTER save as home grid   ` back" : status_.c_str());
    }

private:
    uint32_t lastMs_ = 0;
    std::string status_;
};
