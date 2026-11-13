#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Station.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "lib/Maidenhead.h"

// Repeater directory read from /HamOS/repeaters.csv on the SD card, nearest
// first. One repeater per line:
//   name,frequency MHz,shift MHz,tone,latitude,longitude,note
//   e.g.  MYRPT,145.6000,-0.6,88.5,38.42,27.14,City centre
// Lines starting with # are ignored. See docs/repeaters-example.csv.
class RepeaterApp : public App {
public:
    const char* name() const override { return "Repeaters"; }
    uint16_t color() const override { return 0x9E7F; }

    void onEnter() override {
        load();
        sortByDistance();
    }

    void onKey(const KeyEvent& k) override {
        if (ui::listNav(k, selected_, (int)list_.size())) dirty = true;
    }

    void draw() override {
        if (list_.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 16, "No repeater list", ui::C_WARN, 1.5);
            ui::textCentered(ui::BODY_Y + 42, "Put repeaters.csv in /HamOS on the SD card:", ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 58, "name,MHz,shift,tone,lat,lon,note", ui::C_TEXT);
            ui::textCentered(ui::BODY_Y + 74, "MYRPT,145.6000,-0.6,88.5,38.42,27.14,Town", ui::C_DIM);
            ui::footer("` back");
            return;
        }
        std::vector<std::string> rows;
        char line[64];
        for (const Repeater& r : list_) {
            char where[12] = "";
            if (r.km >= 0) snprintf(where, sizeof(where), "%4.0fkm", r.km);
            snprintf(line, sizeof(line), "%-9.9s %8.4f %+4.1f %-5.5s %s", r.name.c_str(), r.mhz, r.shift, r.tone.c_str(), where);
            rows.push_back(line);
        }
        ui::list(rows, selected_, scroll_);
        const Repeater& r = list_[(size_t)selected_];
        std::string hint = ui::ascii(r.note);
        if (r.km >= 0) {
            snprintf(line, sizeof(line), "%03.0f deg  ", r.bearing);
            hint = line + hint;
        }
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.empty() ? "` back" : hint.c_str());
    }

private:
    struct Repeater {
        std::string name, tone, note;
        double mhz = 0, shift = 0, lat = 0, lon = 0, km = -1, bearing = 0;
        bool hasPosition = false;
    };
    static constexpr size_t MAX_REPEATERS = 200;

    void load() {
        list_.clear();
        selected_ = scroll_ = 0;
        std::string text;
        if (!storage::readFile("/HamOS/repeaters.csv", text)) return;
        for (size_t pos = 0; pos < text.size() && list_.size() < MAX_REPEATERS;) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            for (size_t p = 0; p <= line.size();) {
                size_t comma = f.size() == 6 ? std::string::npos : line.find(',', p);   // the note may contain commas
                if (comma == std::string::npos) comma = line.size();
                f.push_back(line.substr(p, comma - p));
                p = comma + 1;
            }
            if (f.size() < 2 || atof(f[1].c_str()) <= 0) continue;      // also skips a header line
            Repeater r;
            r.name = ui::ascii(f[0]);
            r.mhz = atof(f[1].c_str());
            if (f.size() > 2) r.shift = atof(f[2].c_str());
            if (f.size() > 3) r.tone = f[3];
            if (f.size() > 5 && !f[4].empty() && !f[5].empty()) {
                r.lat = atof(f[4].c_str());
                r.lon = atof(f[5].c_str());
                r.hasPosition = true;
            }
            if (f.size() > 6) r.note = f[6];
            list_.push_back(r);
        }
    }

    void sortByDistance() {
        double lat, lon;
        if (!station::position(lat, lon)) return;
        for (Repeater& r : list_) {
            if (!r.hasPosition) continue;
            r.km = maidenhead::distanceKm(lat, lon, r.lat, r.lon);
            r.bearing = maidenhead::bearingDeg(lat, lon, r.lat, r.lon);
        }
        std::stable_sort(list_.begin(), list_.end(), [](const Repeater& a, const Repeater& b) {
            if ((a.km >= 0) != (b.km >= 0)) return a.km >= 0;       // ones without a position go last
            return a.km < b.km;
        });
    }

    std::vector<Repeater> list_;
    int selected_ = 0, scroll_ = 0;
};
