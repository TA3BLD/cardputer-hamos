#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Flags.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Dxcc.h"
#include "lib/Maidenhead.h"
#include "lib/Sun.h"

// Country reference: flag, callsign prefixes, beam heading and distance
// (short and long path), sunrise and sunset there, and whether it is in the
// log. Type a letter to jump to the countries starting with it.
class CountryApp : public App {
public:
    const char* name() const override { return "Countries"; }
    uint16_t color() const override { return 0xFB2C; }

    void onEnter() override {
        logbook::load();
        if (order_.empty()) {
            size_t count;
            const dxcc::Entity* table = dxcc::table(count);
            for (size_t i = 0; i < count; i++) order_.push_back(&table[i]);
            std::sort(order_.begin(), order_.end(), [](const dxcc::Entity* a, const dxcc::Entity* b) {
                return std::string(a->name) < b->name;
            });
        }
        open_ = false;
    }

    bool onBack() override {
        if (!open_) return false;
        open_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (open_) return;
        if (k.enter) {
            open_ = true;
        } else if (ui::listNav(k, selected_, (int)order_.size())) {
        } else if (isalpha((unsigned char)k.ch)) {
            char c = (char)toupper((unsigned char)k.ch);
            for (size_t i = 0; i < order_.size(); i++)
                if (order_[i]->name[0] == c) { selected_ = (int)i; break; }
        } else {
            return;
        }
        dirty = true;
    }

    void draw() override {
        if (open_) drawCountry(*order_[(size_t)selected_]);
        else drawList();
    }

private:
    void drawList() {
        std::vector<std::string> rows;
        std::vector<uint16_t> colors;
        for (const dxcc::Entity* e : order_) {
            std::string prefixes = e->prefixes;
            if (prefixes.size() > 14) prefixes = prefixes.substr(0, prefixes.rfind(' ', 14)) + "..";
            char line[64];
            snprintf(line, sizeof(line), "    %-17.17s %s", e->name, prefixes.c_str());
            rows.push_back(line);
            colors.push_back(logbook::workedEntity(e) ? ui::C_OK : ui::C_TEXT);
        }
        ui::list(rows, selected_, scroll_, &colors);
        for (int r = 0; r < ui::ROWS && scroll_ + r < (int)order_.size(); r++)
            flags::drawSmall(order_[(size_t)(scroll_ + r)]->iso, 6, ui::BODY_Y + r * ui::ROW_H + 1);
        ui::footer("ENTER details  A-Z jump  green=logged");
    }

    static std::string hhmm(double hours) {
        int minutes = (int)std::lround(std::fmod(hours + 48.0, 24.0) * 60) % 1440;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
        return buf;
    }

    void drawCountry(const dxcc::Entity& e) {
        ui::clearBody();
        int y0 = ui::BODY_Y + 3;
        if (!flags::drawLarge(e.iso, 6, y0)) ui::frame(6, y0, flags::LARGE_W, flags::LARGE_H, ui::C_DIM);
        ui::text(60, y0, e.name, ui::C_SEL, 1.5);
        std::vector<std::string> prefixLines = ui::wrapLines(e.prefixes, 29);
        for (size_t i = 0; i < prefixLines.size() && i < 2; i++) ui::text(60, y0 + 15 + (int)i * 10, prefixLines[i], ui::C_DIM);

        char line[64];
        int y = ui::BODY_Y + 42;
        std::string grid = maidenhead::fromLatLon(e.lat, e.lon).substr(0, 4);
        snprintf(line, sizeof(line), "Centre %s  %.1f%c %.1f%c", grid.c_str(), std::fabs(e.lat), e.lat < 0 ? 'S' : 'N',
                 std::fabs(e.lon), e.lon < 0 ? 'W' : 'E');
        ui::text(6, y, line, ui::C_TEXT);
        double lat, lon;
        bool here = station::position(lat, lon);
        double bearing = 0;
        if (here) {
            double km = maidenhead::distanceKm(lat, lon, e.lat, e.lon);
            bearing = maidenhead::bearingDeg(lat, lon, e.lat, e.lon);
            snprintf(line, sizeof(line), "Short %5.0f km  %03.0f deg", km, bearing);
            ui::text(6, y + 12, line, ui::C_OK);
            snprintf(line, sizeof(line), "Long  %5.0f km  %03.0f deg", 40030.0 - km, std::fmod(bearing + 180.0, 360.0));
            ui::text(6, y + 24, line, ui::C_DIM);
        } else {
            ui::text(6, y + 12, "Set your grid for headings", ui::C_DIM);
        }
        if (net::timeValid()) {
            time_t now = time(nullptr);
            struct tm t;
            gmtime_r(&now, &t);
            sun::Times s = sun::compute(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, e.lat, e.lon);
            if (s.rises) snprintf(line, sizeof(line), "Sun %s - %s UTC", hhmm(s.sunriseUtc).c_str(), hhmm(s.sunsetUtc).c_str());
            else snprintf(line, sizeof(line), "No sunrise or sunset today");
            ui::text(6, y + 36, line, ui::C_NOTICE);
        }
        bool worked = logbook::workedEntity(&e);
        ui::text(6, y + 50, worked ? "In your log" : "Not in your log yet", worked ? ui::C_OK : ui::C_WARN);

        // Compass with the beam heading.
        if (here) {
            int cx = 206, cy = ui::BODY_Y + 72, r = 20;
            ui::circle(cx, cy, r, ui::C_DIM);
            ui::text(cx - 2, cy - r - 9, "N", ui::C_DIM);
            double a = bearing * 3.14159265358979 / 180.0;
            int tx = cx + (int)std::lround(std::sin(a) * (r - 2)), ty = cy - (int)std::lround(std::cos(a) * (r - 2));
            ui::line(cx, cy, tx, ty, ui::C_OK);
            ui::circle(tx, ty, 2, ui::C_OK, true);
        }
        ui::footer("` back to the list");
    }

    std::vector<const dxcc::Entity*> order_;
    bool open_ = false;
    int selected_ = 0, scroll_ = 0;
};
