#pragma once
#include <cstdio>
#include <ctime>
#include "core/App.h"
#include "core/Http.h"
#include "core/Lang.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/SolarXml.h"
#include "lib/Sun.h"

// HF propagation at a glance: solar indices and band conditions from
// hamqsl.com (needs WiFi), plus sunrise/sunset for your location (offline).
class SolarApp : public App {
public:
    const char* name() const override { return T("Solar"); }
    uint16_t color() const override { return 0xFD20; }

    void onEnter() override {
        bool stale = !data_.valid || millis() - fetchedMs_ > 15UL * 60 * 1000;
        phase_ = (stale && net::connected()) ? 1 : 0;
    }

    void onKey(const KeyEvent& k) override {
        if ((k.ch == 'r' || k.ch == 'R') && net::connected()) {
            phase_ = 1;
            dirty = true;
        }
    }

    void onTick(uint32_t) override {
        if (phase_ != 2) return;        // the "Loading" screen has been drawn: fetch now (blocks a moment)
        fetch();
        phase_ = 0;
        dirty = true;
    }

    void draw() override {
        ui::clearBody();
        if (phase_ == 1) {
            ui::textCentered(ui::BODY_Y + 40, T("Loading solar data..."), ui::C_DIM, 1.5);
            ui::footer(T("hamqsl.com"));
            phase_ = 2;
            return;
        }
        char line[64];
        if (data_.valid) {
            snprintf(line, sizeof(line), "SFI %s  SN %s  A %s  K %s  %s", data_.flux.c_str(), data_.sunspots.c_str(),
                     data_.aIndex.c_str(), data_.kIndex.c_str(), data_.xray.c_str());
            ui::text(6, ui::BODY_Y + 4, line, ui::C_TEXT);
            ui::text(6, ui::BODY_Y + 20, T("Band"), ui::C_DIM);
            ui::text(96, ui::BODY_Y + 20, T("Day"), ui::C_DIM);
            ui::text(166, ui::BODY_Y + 20, T("Night"), ui::C_DIM);
            for (int r = 0; r < 4; r++) {
                int y = ui::BODY_Y + 32 + r * 14;
                ui::text(6, y, solar::bandName(r), ui::C_TEXT, 1.5);
                for (int c = 0; c < 2; c++) ui::text(96 + c * 70, y, data_.cond[r][c], condColor(data_.cond[r][c]), 1.5);
            }
        } else {
            ui::textCentered(ui::BODY_Y + 30, net::connected() ? error_ : T("No WiFi: solar data unavailable"), ui::C_WARN);
        }

        double lat, lon;
        time_t now = time(nullptr);
        if (station::position(lat, lon) && net::timeValid()) {
            struct tm t;
            gmtime_r(&now, &t);
            sun::Times s = sun::compute(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, lat, lon);
            double offset = atof(config::get("utc_offset", "3").c_str());
            if (s.rises) snprintf(line, sizeof(line), "Sunrise %s  Sunset %s  (local)", hhmm(s.sunriseUtc + offset).c_str(),
                                  hhmm(s.sunsetUtc + offset).c_str());
            else snprintf(line, sizeof(line), "Sun does not rise or set today");
            ui::text(6, ui::BODY_Y + 94, line, 0xFFE0);
        } else {
            ui::text(6, ui::BODY_Y + 94, T("Sunrise/sunset: needs grid and time"), ui::C_DIM);
        }
        ui::footer(net::connected() ? T("R refresh   ` back") : T("` back"));
    }

private:
    static uint16_t condColor(const std::string& c) {
        return c == "Good" ? ui::C_OK : c == "Fair" ? ui::C_WARN : c == "Poor" ? ui::C_ERR : ui::C_DIM;
    }

    static std::string hhmm(double hours) {
        hours = fmod(hours + 48.0, 24.0);
        int minutes = (int)(hours * 60 + 0.5) % 1440;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
        return buf;
    }

    void fetch() {
        // hamqsl.com answers plain HTTP with a redirect to HTTPS, so go there directly.
        http::Request r;
        r.url = "https://www.hamqsl.com/solarxml.php";
        r.maxBytes = 16 * 1024;
        http::Response a = http::fetch(r);
        if (!a.ok()) {
            error_ = std::string("hamqsl.com: ") + T(a.error.c_str());
            return;
        }
        solar::Data d = solar::parse(a.body.c_str());
        if (d.valid) {
            data_ = d;
            fetchedMs_ = millis();
            error_.clear();
        } else {
            error_ = T("Unexpected answer from hamqsl.com");
        }
    }

    solar::Data data_;
    std::string error_ = T("No data yet. Press R.");
    uint32_t fetchedMs_ = 0;
    int phase_ = 0;         // 1: show "Loading", 2: fetch on the next tick
};
