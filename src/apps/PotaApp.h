#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Pota.h"
#include "core/Rig.h"
#include "core/Ui.h"

// Parks and Summits on the Air: live spots, the parks and summits near you,
// and your own activation (contact count, spotting yourself).
// TAB switches between the five pages.
class PotaApp : public App {
public:
    const char* name() const override { return T("POTA/SOTA"); }
    uint16_t color() const override { return 0x2E8B; }

    void onEnter() override {
        logbook::load();
        if (page_ == POTA_SPOTS && spots_[0].empty() && net::connected()) loadPending_ = true;
        detail_ = false;
        editing_ = 0;
    }

    void onTick(uint32_t now) override {
        if (loadPending_ && !dirty) {      // the "Loading" line has been drawn
            loadPending_ = false;
            load();
            dirty = true;
        }
        if (now - lastTickMs_ > 1000) {
            lastTickMs_ = now;
            if (page_ == ACTIVATION) dirty = true;
        }
    }

    bool onBack() override {
        if (editing_) { editing_ = 0; dirty = true; return true; }
        if (detail_) { detail_ = false; dirty = true; return true; }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (editing_) { editKey(k); return; }
        if (k.tab) {
            page_ = (Page)((page_ + 1) % PAGES);
            selected_ = scroll_ = 0;
            detail_ = false;
            message_.clear();
            if ((page_ == POTA_SPOTS || page_ == SOTA_SPOTS) && spots_[page_].empty() && net::connected()) startLoad();
            if (page_ == PARKS || page_ == SUMMITS) loadNearby();
            dirty = true;
            return;
        }
        if (page_ == ACTIVATION) { activationKey(k); return; }
        int count = page_ <= SOTA_SPOTS ? (int)spots_[page_].size() : (int)places_.size();
        if (!detail_ && ui::listNav(k, selected_, count)) { dirty = true; return; }
        if (k.enter && count) { detail_ = !detail_; dirty = true; return; }
        if (k.ch == 'r' || k.ch == 'R') {
            if (page_ <= SOTA_SPOTS) startLoad();
            else loadNearby();
            dirty = true;
            return;
        }
        if (page_ <= SOTA_SPOTS && (k.ch == 't' || k.ch == 'T') && selected_ < count) {
            const pota::Spot& s = spots_[page_][selected_];
            message_ = rig::tune(s.freqKhz / 1000, s.mode) ? T("Radio tuned") : T("No radio connected");
            dirty = true;
            return;
        }
        if ((page_ == PARKS || page_ == SUMMITS) && (k.ch == 'd' || k.ch == 'D')) { download(); return; }
        if ((page_ == PARKS || page_ == SUMMITS) && (k.ch == 's' || k.ch == 'S') && selected_ < count) {
            config::set(page_ == PARKS ? "pota_ref" : "sota_ref", places_[selected_].ref);
            config::save();
            message_ = std::string(T("Activating")) + " " + places_[selected_].ref;
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        static const char* titles[] = {"POTA spots", "SOTA spots", "Parks near me", "Summits near me", "My activation"};
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, T(titles[page_]), ui::C_NOTICE);
        ui::text(ui::W - 30, ui::BODY_Y + 2, std::to_string(page_ + 1) + "/5", ui::C_DIM);
        if (loadPending_) {
            ui::textCentered(60, T("Loading..."), ui::C_DIM);
            ui::footer("");
            return;
        }
        if (page_ == ACTIVATION) { drawActivation(); return; }
        if (page_ <= SOTA_SPOTS) drawSpots();
        else drawPlaces();
    }

private:
    enum Page { POTA_SPOTS, SOTA_SPOTS, PARKS, SUMMITS, ACTIVATION, PAGES };
    Page page_ = POTA_SPOTS;
    std::vector<pota::Spot> spots_[2];
    std::vector<pota::Place> places_;
    int selected_ = 0, scroll_ = 0;
    bool detail_ = false, loadPending_ = false;
    std::string message_;
    uint32_t lastTickMs_ = 0;
    int editing_ = 0;                       // self-spot form: 1 frequency, 2 mode, 3 comment
    ui::TextInput freqIn_, modeIn_, commentIn_;
    static inline PotaApp* self = nullptr;

    static const int kTop = ui::BODY_Y + 13;

    void startLoad() {
        loadPending_ = true;
        message_.clear();
    }

    void load() {
        std::string err;
        pota::Program p = page_ == SOTA_SPOTS ? pota::SOTA : pota::POTA;
        if (!pota::fetchSpots(p, spots_[p], err)) message_ = err;
        selected_ = scroll_ = 0;
    }

    void loadNearby() {
        std::string err;
        places_ = pota::nearby(page_ == PARKS ? pota::POTA : pota::SOTA, 60, err);
        message_ = err;
        selected_ = scroll_ = 0;
    }

    void download() {
        self = this;
        ui::fill(0, kTop, ui::W, ui::BODY_H - 13, ui::C_BG);
        ui::textCentered(50, T("Downloading the list..."), ui::C_DIM);
        size_t kept = 0;
        std::string err;
        double km = atof(config::get("near_km", "150").c_str());
        bool ok = pota::downloadNearby(page_ == PARKS ? pota::POTA : pota::SOTA, km > 0 ? km : 150, [](long done, long total) {
            static long lastKb = -1;
            long kb = done / 102400;
            if (kb == lastKb) return;
            lastKb = kb;
            char b[48];
            if (total > 0) snprintf(b, sizeof(b), "%ld / %ld KB", done / 1024, total / 1024);
            else snprintf(b, sizeof(b), "%ld KB", done / 1024);
            ui::fill(0, 66, ui::W, 12, ui::C_BG);
            ui::textCentered(66, b, ui::C_TEXT);
        }, kept, err);
        loadNearby();
        char b[64];
        snprintf(b, sizeof(b), "%s: %u", T("Saved nearby"), (unsigned)kept);
        message_ = ok ? std::string(b) : err;
        dirty = true;
    }

    static std::string ago(uint32_t t) {
        if (!t || !net::timeValid()) return "";
        long s = (long)(net::utcMillis() / 1000) - (long)t;
        if (s < 0) s = 0;
        return s < 3600 ? std::to_string(s / 60) + "m" : std::to_string(s / 3600) + "h";
    }

    void drawSpots() {
        const std::vector<pota::Spot>& v = spots_[page_];
        if (v.empty()) {
            ui::textCentered(55, message_.empty() ? (net::connected() ? T("No spots") : T("No WiFi")) : message_, ui::C_DIM);
            ui::footer(T("R refresh  TAB page  ` back"));
            return;
        }
        if (detail_ && selected_ < (int)v.size()) {
            const pota::Spot& s = v[selected_];
            int y = kTop + 1;
            char line[64];
            snprintf(line, sizeof(line), "%s  %.1f kHz %s", s.activator.c_str(), s.freqKhz, s.mode.c_str());
            ui::text(4, y, line, ui::C_OK);
            y += 12;
            ui::text(4, y, s.ref + "  " + s.location, ui::C_NOTICE);
            y += 12;
            for (const std::string& l : ui::wrapLines(s.name, 38)) { ui::text(4, y, l); y += 11; if (y > 95) break; }
            for (const std::string& l : ui::wrapLines(s.comment, 38)) { ui::text(4, y, l, ui::C_DIM); y += 11; if (y > 105) break; }
            std::string by = std::string(T("by")) + " " + s.spotter + " " + ago(s.unixTime);
            if (s.km >= 0) by += "  " + std::to_string((int)s.km) + " km";
            ui::text(4, 108, by, ui::C_DIM);
            ui::footer(message_.empty() ? T("T tune radio  ENTER list") : message_.c_str());
            return;
        }
        std::vector<std::string> rows;
        for (const pota::Spot& s : v) {
            char line[64];
            snprintf(line, sizeof(line), "%6.0f %-4.4s %-9.9s %-10.10s %s", s.freqKhz, s.mode.c_str(), s.activator.c_str(), s.ref.c_str(),
                     ago(s.unixTime).c_str());
            rows.push_back(line);
        }
        ui::list(rows, selected_, scroll_, nullptr, kTop, ui::ROWS - 1);
        ui::footer(message_.empty() ? T("ENTER info  T tune  R refresh") : message_.c_str());
    }

    void drawPlaces() {
        if (places_.empty()) {
            int y = 45;
            for (const std::string& l : ui::wrapLines(message_.empty() ? T("Nothing nearby") : message_, 36)) {
                ui::textCentered(y, l, ui::C_DIM);
                y += 12;
            }
            ui::textCentered(85, T("D downloads the list (WiFi, a few MB)"), ui::C_DIM);
            ui::footer(T("D download  TAB page  ` back"));
            return;
        }
        if (detail_ && selected_ < (int)places_.size()) {
            const pota::Place& p = places_[selected_];
            int y = kTop + 2;
            ui::text(4, y, p.ref, ui::C_OK, 1.5);
            y += 18;
            for (const std::string& l : ui::wrapLines(p.name, 38)) { ui::text(4, y, l); y += 11; }
            ui::text(4, y, p.info, ui::C_DIM);
            y += 13;
            char b[64];
            snprintf(b, sizeof(b), "%.0f km  %03.0f deg  %.4f %.4f", p.km, p.bearing, p.lat, p.lon);
            ui::text(4, y, b, ui::C_NOTICE);
            ui::footer(message_.empty() ? T("S activate here  ENTER list") : message_.c_str());
            return;
        }
        std::vector<std::string> rows;
        for (const pota::Place& p : places_) {
            char line[80];
            snprintf(line, sizeof(line), "%-11.11s %4.0fkm %s", p.ref.c_str(), p.km, p.name.c_str());
            rows.push_back(ui::displayable(line).substr(0, 60));
        }
        ui::list(rows, selected_, scroll_, nullptr, kTop, ui::ROWS - 1);
        ui::footer(message_.empty() ? T("ENTER info  S activate  D download") : message_.c_str());
    }

    // ---- my activation ----
    void drawActivation() {
        std::string park = config::get("pota_ref"), summit = config::get("sota_ref");
        int y = kTop + 2;
        if (park.empty() && summit.empty()) {
            for (const std::string& l : ui::wrapLines(T("Pick a park or summit on the Nearby pages (S), or set POTA park ref / SOTA summit ref in Settings."), 38)) {
                ui::text(4, y, l, ui::C_DIM);
                y += 11;
            }
            ui::footer(T("TAB page  ` back"));
            return;
        }
        if (!park.empty()) {
            int n = pota::activationCount(pota::POTA);
            ui::text(4, y, T("POTA ") + park, ui::C_NOTICE, 1.5);
            char b[32];
            snprintf(b, sizeof(b), "%d/10", n);
            ui::text(160, y, b, n >= 10 ? ui::C_OK : ui::C_WARN, 1.5);
            y += 20;
        }
        if (!summit.empty()) {
            int n = pota::activationCount(pota::SOTA);
            ui::text(4, y, T("SOTA ") + summit, ui::C_NOTICE, 1.5);
            char b[32];
            snprintf(b, sizeof(b), "%d/4", n);
            ui::text(160, y, b, n >= 4 ? ui::C_OK : ui::C_WARN, 1.5);
            y += 20;
        }
        ui::text(4, y, T("contacts today (UTC); goal shown right"), ui::C_DIM);
        y += 14;
        if (editing_) {
            freqIn_.draw(y, "kHz");
            modeIn_.draw(y + 12, T("Mode"));
            commentIn_.draw(y + 24, T("Note"));
            const int rowY[] = {y, y + 12, y + 24};
            ui::text(0, rowY[editing_ - 1], ">", ui::C_NOTICE);
            ui::footer(T("ENTER next/send  ` cancel"));
            return;
        }
        if (!message_.empty()) ui::text(4, y, message_, ui::C_OK);
        ui::footer(park.empty() ? T("TAB page  ` back") : T("S spot me on POTA  TAB page"));
    }

    void activationKey(const KeyEvent& k) {
        if ((k.ch == 's' || k.ch == 'S') && !config::get("pota_ref").empty()) {
            std::string f = rig::freqMhz();
            char b[16] = "";
            if (!f.empty()) snprintf(b, sizeof(b), "%.1f", atof(f.c_str()) * 1000);
            freqIn_.value = b;
            freqIn_.maxLen = 9;
            modeIn_.value = rig::mode().empty() ? "SSB" : (rig::mode() == "DATA" ? "FT8" : rig::mode());
            modeIn_.maxLen = 6;
            modeIn_.upper = true;
            commentIn_.value = "";
            commentIn_.maxLen = 40;
            editing_ = 1;
            message_.clear();
            dirty = true;
        }
    }

    void editKey(const KeyEvent& k) {
        ui::TextInput* in = editing_ == 1 ? &freqIn_ : editing_ == 2 ? &modeIn_ : &commentIn_;
        if (k.enter) {
            if (editing_ < 3) { editing_++; dirty = true; return; }
            std::string err;
            double khz = atof(freqIn_.value.c_str());
            if (khz < 1800) {
                message_ = T("Enter the frequency in kHz");
                editing_ = 1;
            } else if (pota::selfSpot(config::get("pota_ref"), khz, modeIn_.value, commentIn_.value, err)) {
                message_ = T("Spotted on pota.app");
                editing_ = 0;
                spots_[0].clear();
            } else {
                message_ = err;
                editing_ = 0;
            }
            dirty = true;
            return;
        }
        if (k.tab) { editing_ = editing_ % 3 + 1; dirty = true; return; }
        if (in->handle(k)) dirty = true;
    }
};
