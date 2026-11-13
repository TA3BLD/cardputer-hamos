#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Station.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "lib/Sgp4.h"

// Amateur satellite tracking: where a satellite is right now, the Doppler
// shift, and its next passes over your location. Orbit data comes from
// celestrak.org (press U with WiFi) and is kept on the SD card, so prediction
// works offline afterwards; refresh it every week or two.
class SatApp : public App {
public:
    const char* name() const override { return "Satellites"; }
    uint16_t color() const override { return 0xC618; }

    void onEnter() override {
        if (!loaded_) {
            std::string text;
            if (storage::readFile(kPath, text)) parseTle(text);
            loaded_ = true;
        }
        open_ = false;
        phase_ = IDLE;
        status_.clear();
    }

    bool onBack() override {
        if (!open_) return false;
        open_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (open_) return;
        if (k.ch == 'u' || k.ch == 'U') {
            if (!net::connected()) status_ = "No WiFi connection";
            else phase_ = DOWNLOAD_PENDING;
        } else if (k.enter && !entries_.empty()) {
            const Entry& e = entries_[(size_t)selected_];
            sat_ = sgp4::fromTle(e.name, e.line1, e.line2);
            passes_.clear();
            open_ = true;
            phase_ = PASSES_PENDING;
        } else if (!ui::listNav(k, selected_, (int)entries_.size())) {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (phase_ == DOWNLOAD_NOW) {           // the "Downloading" screen has been drawn
            download();
            phase_ = IDLE;
            dirty = true;
        } else if (phase_ == PASSES_NOW) {
            computePasses();
            phase_ = IDLE;
            dirty = true;
        }
        if (open_ && now - lastMs_ >= 1000) {   // live position
            lastMs_ = now;
            dirty = true;
        }
    }

    void draw() override {
        if (phase_ == DOWNLOAD_PENDING) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 40, "Downloading orbit data...", ui::C_DIM, 1.5);
            ui::footer("celestrak.org");
            phase_ = DOWNLOAD_NOW;
            return;
        }
        if (open_) drawSatellite();
        else drawList();
    }

private:
    enum Phase { IDLE, DOWNLOAD_PENDING, DOWNLOAD_NOW, PASSES_PENDING, PASSES_NOW };
    struct Entry {
        std::string name, line1, line2;
    };
    static constexpr const char* kPath = "/HamOS/tle.txt";
    static constexpr size_t MAX_SATS = 150;

    void parseTle(const std::string& text) {
        entries_.clear();
        std::string lines[3];
        int have = 0;
        for (size_t pos = 0; pos < text.size() && entries_.size() < MAX_SATS;) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            // A set is a name line followed by the lines starting "1 " and "2 ".
            if (have == 1 && line.compare(0, 2, "1 ") != 0) have = 0;
            if (have == 2 && line.compare(0, 2, "2 ") != 0) have = 0;
            lines[have++] = line;
            if (have == 3) {
                entries_.push_back({lines[0], lines[1], lines[2]});
                have = 0;
            }
        }
        if (selected_ >= (int)entries_.size()) selected_ = 0;
    }

    void download() {
        WiFiClientSecure client;
        client.setInsecure();           // public, read-only data
        HTTPClient http;
        http.setTimeout(10000);
        if (!http.begin(client, "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle")) {
            status_ = "Could not start the download";
            return;
        }
        int code = http.GET();
        if (code == 200) {
            std::string body = http.getString().c_str();
            parseTle(body);
            if (entries_.empty()) status_ = "Unexpected answer from celestrak.org";
            else status_ = storage::writeFile(kPath, body) ? "Orbit data updated" : "Updated (no SD card: not saved)";
        } else {
            status_ = "Download failed (" + std::to_string(code) + ")";
        }
        http.end();
    }

    void computePasses() {
        passes_.clear();
        double lat, lon;
        if (!sat_.valid || !net::timeValid() || !station::position(lat, lon)) return;
        double t = (double)time(nullptr);
        for (int i = 0; i < 4; i++) {
            sgp4::Pass p = sgp4::nextPass(sat_, t, lat, lon, 0, 36);
            if (!p.found) break;
            passes_.push_back(p);
            t = p.los + 60;
        }
    }

    static const char* compass(double az) {
        static const char* names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        return names[(int)((az + 22.5) / 45.0) % 8];
    }

    void drawList() {
        if (entries_.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 28, "No orbit data", ui::C_WARN, 1.5);
            ui::textCentered(ui::BODY_Y + 54, "Press U to download it (needs WiFi)", ui::C_DIM);
            ui::footer(status_.empty() ? "U update   ` back" : status_.c_str());
            return;
        }
        std::vector<std::string> names;
        for (const Entry& e : entries_) names.push_back(ui::ascii(e.name));
        ui::list(names, selected_, scroll_);
        char hint[48];
        snprintf(hint, sizeof(hint), "ENTER passes   U update   %d sats", (int)entries_.size());
        ui::footer(status_.empty() ? hint : status_.c_str());
    }

    void drawSatellite() {
        ui::clearBody();
        ui::text(6, ui::BODY_Y + 3, ui::ascii(sat_.name), ui::C_SEL, 1.5);
        double lat, lon;
        if (!sat_.valid) {
            ui::textCentered(ui::BODY_Y + 44, "This orbit is not supported", ui::C_WARN);
            ui::textCentered(ui::BODY_Y + 58, "(only low orbits are calculated)", ui::C_DIM);
        } else if (!net::timeValid()) {
            ui::textCentered(ui::BODY_Y + 50, "Needs the time: WiFi or GPS", ui::C_WARN);
        } else if (!station::position(lat, lon)) {
            ui::textCentered(ui::BODY_Y + 50, "Needs your position: grid or GPS", ui::C_WARN);
        } else {
            char line[64];
            sgp4::Look now = sgp4::look(sat_, (double)time(nullptr), lat, lon);
            if (!now.valid) {
                ui::text(6, ui::BODY_Y + 20, "Orbit data too old: press U in the list", ui::C_WARN);
            } else {
                snprintf(line, sizeof(line), "Az %03.0f  El %+.0f  %.0f km", now.azimuth, now.elevation, now.rangeKm);
                ui::text(6, ui::BODY_Y + 20, line, now.elevation > 0 ? ui::C_OK : ui::C_TEXT, 1.5);
                snprintf(line, sizeof(line), "Doppler  2m %+.1f   70cm %+.1f kHz",
                         sgp4::dopplerHz(now.rangeRateKmS, 145.9) / 1000, sgp4::dopplerHz(now.rangeRateKmS, 435.5) / 1000);
                ui::text(6, ui::BODY_Y + 36, line, now.elevation > 0 ? ui::C_TEXT : ui::C_DIM);
            }
            if (phase_ == PASSES_PENDING) {
                ui::text(6, ui::BODY_Y + 54, "Calculating passes...", ui::C_DIM);
                phase_ = PASSES_NOW;
            } else if (passes_.empty()) {
                ui::text(6, ui::BODY_Y + 54, "No pass in the next 36 hours", ui::C_DIM);
            } else {
                double offset = atof(config::get("utc_offset", "3").c_str()) * 3600.0;
                ui::text(6, ui::BODY_Y + 50, "Rise (local)  Max   Min  Path", ui::C_DIM);
                for (size_t i = 0; i < passes_.size(); i++) {
                    const sgp4::Pass& p = passes_[i];
                    time_t local = (time_t)(p.aos + offset);
                    struct tm t;
                    gmtime_r(&local, &t);
                    char when[16];
                    strftime(when, sizeof(when), "%d.%m %H:%M", &t);
                    snprintf(line, sizeof(line), "%s   %2.0f    %2.0f   %s>%s", when, p.maxElevation, (p.los - p.aos) / 60.0,
                             compass(p.aosAzimuth), compass(p.losAzimuth));
                    ui::text(6, ui::BODY_Y + 62 + (int)i * ui::ROW_H - 1, line, p.maxElevation >= 20 ? ui::C_OK : ui::C_TEXT);
                }
            }
        }
        ui::footer("` back to the list");
    }

    std::vector<Entry> entries_;
    std::vector<sgp4::Pass> passes_;
    sgp4::Satellite sat_;
    bool loaded_ = false, open_ = false;
    Phase phase_ = IDLE;
    int selected_ = 0, scroll_ = 0;
    uint32_t lastMs_ = 0;
    std::string status_;
};
