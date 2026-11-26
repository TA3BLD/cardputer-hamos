#pragma once
#include <M5Cardputer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/Lang.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Maidenhead.h"
#include "lib/Sun.h"

// Home screen, shown at start and after a while idle in the app menu.
// TAB cycles the pages: summary (clock, station, status), map, position, messages.
// ENTER or ` opens the app menu.
class HomeApp : public App {
public:
    App* menu = nullptr;    // the app menu (launcher), set in main.cpp
    App* map = nullptr;     // the Map app, drawn on the map page

    const char* name() const override {
        switch (page_) {
            case MAP: return T("Home - map");
            case POSITION: return T("Home - position");
            case MESSAGES: return T("Home - messages");
            default: return T("Home screen");
        }
    }
    uint16_t color() const override { return 0x05FF; }

    void onEnter() override {
        if (page_ == MAP && map) map->onEnter();
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) {
            page_ = (page_ + 1) % PAGES;
            if (page_ == MAP && map) map->onEnter();
            dirty = true;
            return;
        }
        if (k.enter) {
            openNext = menu;
            return;
        }
        if (page_ == MAP && map) {      // arrows, zoom and layers as in the Map app
            map->onKey(k);
            dirty = dirty || map->dirty;
            map->dirty = false;
        }
    }

    void onTick(uint32_t now) override {
        uint32_t every = page_ == MAP ? 10000 : 1000;
        if (now - lastMs_ >= every) {
            lastMs_ = now;
            dirty = true;
        }
    }

    void draw() override {
        if (page_ == MAP && map) {
            map->draw();
            map->dirty = false;
            return;
        }
        ui::clearBody();
        if (page_ == POSITION) drawPosition();
        else if (page_ == MESSAGES) drawMessages();
        else drawSummary();
        dots();
    }

private:
    enum { SUMMARY, MAP, POSITION, MESSAGES, PAGES };
    int page_ = SUMMARY;
    uint32_t lastMs_ = 0;

    // Page dots and the keys, in the footer.
    void dots() {
        ui::footer(T("TAB page   ENTER apps"));
        for (int i = 0; i < PAGES; i++)
            ui::circle(ui::W - 44 + i * 10, ui::H - ui::FOOTER_H / 2 - 1, 3, i == page_ ? ui::C_SEL : ui::C_DIM, i == page_);
    }

    static double utcOffset() { return atof(config::get("utc_offset", "3").c_str()); }

    // "06:12" from hours since midnight (UTC) plus the local offset.
    static std::string hhmm(double hoursUtc, double offset) {
        double h = std::fmod(hoursUtc + offset + 48.0, 24.0);
        int m = (int)std::lround(h * 60) % (24 * 60);
        char b[8];
        snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
        return b;
    }

    static bool sunToday(sun::Times& t) {
        double lat, lon;
        if (!net::timeValid() || !station::position(lat, lon)) return false;
        time_t now = time(nullptr);
        struct tm u;
        gmtime_r(&now, &u);
        t = sun::compute(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, lat, lon);
        return true;
    }

    void drawSummary() {
        const int top = ui::BODY_Y;
        double offset = utcOffset();
        if (net::timeValid()) {
            time_t now = time(nullptr);
            struct tm u, l;
            gmtime_r(&now, &u);
            time_t local = now + (time_t)(offset * 3600);
            gmtime_r(&local, &l);
            char b[32];
            snprintf(b, sizeof(b), "%02d:%02d", u.tm_hour, u.tm_min);
            ui::text(6, top + 6, b, ui::C_TEXT, 4);
            ui::text(130, top + 6, "UTC", ui::C_DIM);
            snprintf(b, sizeof(b), ":%02d", u.tm_sec);
            ui::text(126, top + 22, b, ui::C_DIM, 1.5);
            snprintf(b, sizeof(b), "%s %02d:%02d", T("Local"), l.tm_hour, l.tm_min);
            ui::text(160, top + 8, b, ui::C_OK);
            strftime(b, sizeof(b), "%d.%m.%Y", &l);
            ui::text(160, top + 22, b, ui::C_DIM);
        } else {
            ui::text(6, top + 6, "--:--", ui::C_DIM, 4);
            ui::text(130, top + 10, net::configured() ? T("Waiting for the time") : T("No WiFi: no time"), ui::C_WARN);
        }

        // Station
        int y = top + 42;
        ui::line(4, y - 4, ui::W - 4, y - 4, ui::C_FOOT);
        std::string call = config::get("callsign");
        ui::text(6, y, call.empty() ? T("No callsign") : call, call.empty() ? ui::C_WARN : ui::C_NOTICE, 2);
        std::string grid = station::grid();
        ui::text(132, y + 4, grid.empty() ? "-" : grid, ui::C_TEXT, 1.5);
        y += 22;

        // Status line: battery, WiFi, GPS, unread messages
        char b[48];
        int bat = (int)M5Cardputer.Power.getBatteryLevel();
        snprintf(b, sizeof(b), "%s %d%%", T("Bat"), bat);
        int x = 6;
        auto chip = [&](const std::string& s, uint16_t c) {
            ui::text(x, y, s, c);
            x += (int)ui::glyphCount(s) * 6 + 10;
        };
        chip(b, bat > 20 ? ui::C_OK : ui::C_ERR);
        chip(net::connected() ? "WiFi" : T("no WiFi"), net::connected() ? ui::C_OK : ui::C_DIM);
        chip(gps::hasFix() ? "GPS " + std::to_string(gps::fix().satellites) : gps::present() ? T("GPS search") : T("no GPS"),
             gps::hasFix() ? ui::C_OK : ui::C_DIM);
        if (mesh::unread() > 0) chip("Mesh " + std::to_string(mesh::unread()), ui::C_NOTICE);
        if (aprsis::unread() > 0) chip("APRS " + std::to_string(aprsis::unread()), ui::C_NOTICE);
        y += 13;

        // Sun
        sun::Times t;
        if (sunToday(t)) {
            std::string s = t.rises ? std::string(T("Sunrise")) + " " + hhmm(t.sunriseUtc, offset) + "   " + T("Sunset") + " " +
                                          hhmm(t.sunsetUtc, offset)
                                    : std::string(T("No sunrise / sunset today"));
            ui::text(6, y, s, ui::C_DIM);
        }
        y += 11;
        if (net::connected()) ui::text(6, y, net::address() + "  " + net::hostname() + ".local", ui::C_DIM);
    }

    void drawPosition() {
        const int top = ui::BODY_Y + 4;
        double lat, lon;
        bool have = station::position(lat, lon);
        bool live = gps::hasFix();
        if (!have) {
            ui::textCentered(top + 30, T("No position"), ui::C_WARN, 1.5);
            ui::textCentered(top + 54, T("Waiting for GPS, or set the grid in Settings"), ui::C_DIM);
            return;
        }
        ui::text(6, top, station::grid(), ui::C_NOTICE, 2.5);
        ui::text(170, top + 4, live ? T("GPS live") : T("from grid"), live ? ui::C_OK : ui::C_WARN);
        char b[64];
        int y = top + 26;
        snprintf(b, sizeof(b), "%.5f %c   %.5f %c", std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon), lon >= 0 ? 'E' : 'W');
        ui::text(6, y, b, ui::C_TEXT, 1.5);
        y += 16;
        if (live) {
            const nmea::Fix& f = gps::fix();
            snprintf(b, sizeof(b), "%s %.0f m   %s %.0f km/h   %s %.0f", T("Alt"), f.altitudeM, T("Speed"), f.speedKmh, T("Course"),
                     f.courseDeg);
            ui::text(6, y, b, ui::C_TEXT);
            y += 12;
            snprintf(b, sizeof(b), "%s %d", T("Satellites"), f.satellites);
            ui::text(6, y, b, ui::C_DIM);
            y += 12;
        } else {
            ui::text(6, y, gps::present() ? T("GPS is looking for satellites") : T("No GPS receiver"), ui::C_DIM);
            y += 12;
        }
        sun::Times t;
        if (sunToday(t) && t.rises) {
            double offset = utcOffset();
            snprintf(b, sizeof(b), "%s %s  %s %s  (UTC%+g)", T("Sunrise"), hhmm(t.sunriseUtc, offset).c_str(), T("Sunset"),
                     hhmm(t.sunsetUtc, offset).c_str(), offset);
            ui::text(6, y, b, ui::C_DIM);
        }
    }

    // The newest Meshtastic and APRS messages together, newest at the bottom.
    void drawMessages() {
        struct Line {
            long long when;
            std::string text;
            uint16_t color;
        };
        std::vector<Line> all;
        uint32_t nowMs = millis();
        long long nowUtc = net::timeValid() ? (long long)time(nullptr) : 0;
        for (const mesh::Message& m : mesh::messages()) {
            std::string who = m.mine ? T("me") : mesh::nodeName(m.from, true);
            long long when = m.utc ? (long long)m.utc : 0;
            all.push_back({when, "M " + who + ": " + m.text, m.mine ? ui::C_DIM : (uint16_t)0xF81F});
        }
        for (const aprsis::Message& m : aprsis::messages()) {
            long long when = m.utcMs ? m.utcMs / 1000 : nowUtc ? nowUtc - (long long)((nowMs - m.atMs) / 1000) : 0;
            all.push_back({when, "A " + (m.mine ? "> " + m.to : m.from) + ": " + m.text, m.mine ? ui::C_DIM : (uint16_t)0xAEDC});
        }
        std::stable_sort(all.begin(), all.end(), [](const Line& a, const Line& b) { return a.when < b.when; });
        if (all.empty()) {
            ui::textCentered(ui::BODY_Y + 36, T("No messages"), ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 60, T("Meshtastic and APRS messages show here"), ui::C_DIM);
            return;
        }
        std::vector<std::pair<std::string, uint16_t>> lines;
        for (const Line& l : all)
            for (const std::string& part : ui::wrapLines(ui::displayable(l.text), 38)) lines.push_back({part, l.color});
        const int rows = ui::ROWS;
        size_t first = lines.size() > (size_t)rows ? lines.size() - rows : 0;
        for (size_t i = first; i < lines.size(); i++)
            ui::text(4, ui::BODY_Y + 2 + (int)(i - first) * ui::ROW_H, lines[i].first, lines[i].second);
    }
};
