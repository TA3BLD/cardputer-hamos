#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Log.h"
#include "core/Ui.h"
#include "lib/Awards.h"

// Award progress from the whole logbook (worked): DXCC countries, continents,
// Turkish provinces, grid squares. TAB switches page.
class AwardsApp : public App {
public:
    const char* name() const override { return T("Awards"); }
    uint16_t color() const override { return 0xFEA0; }

    void onEnter() override {
        pending_ = true;
        scroll_ = 0;
    }

    void onTick(uint32_t) override {
        if (pending_ && !dirty) {
            pending_ = false;
            tally_ = awards::Tally();
            logbook::forEach([](const adif::Qso& q, void* t) { ((awards::Tally*)t)->add(q); }, &tally_);
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) { page_ = (page_ + 1) % 4; scroll_ = 0; dirty = true; return; }
        if (k.down()) { scroll_++; dirty = true; }
        if (k.up() && scroll_ > 0) { scroll_--; dirty = true; }
    }

    void draw() override {
        ui::clearBody();
        static const char* titles[] = {"DXCC countries", "Continents (WAC)", "Turkish provinces", "Grid squares"};
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, T(titles[page_]), ui::C_NOTICE);
        ui::text(ui::W - 30, ui::BODY_Y + 2, std::to_string(page_ + 1) + "/4", ui::C_DIM);
        if (pending_) {
            ui::textCentered(60, T("Counting the log..."), ui::C_DIM);
            ui::footer("");
            return;
        }
        int y = ui::BODY_Y + 16;
        char b[64];
        if (page_ == 0) {
            snprintf(b, sizeof(b), "%u", (unsigned)tally_.countries.size());
            ui::text(6, y, b, ui::C_OK, 3);
            ui::text(70, y + 4, T("countries worked"), ui::C_TEXT);
            snprintf(b, sizeof(b), "%s: %u", T("contacts"), (unsigned)tally_.contacts);
            ui::text(70, y + 16, b, ui::C_DIM);
            y += 32;
            drawBands(y, [&](const std::string& band) { return tally_.countriesByBand.count(band) ? tally_.countriesByBand.at(band).size() : 0; });
        } else if (page_ == 1) {
            static const struct { const char* code; const char* name; } c[] = {
                {"EU", "Europe"}, {"AS", "Asia"}, {"AF", "Africa"}, {"NA", "North America"}, {"SA", "South America"}, {"OC", "Oceania"}};
            for (const auto& x : c) {
                bool ok = tally_.continents.count(x.code) > 0;
                ui::text(10, y, ok ? "[x]" : "[ ]", ok ? ui::C_OK : ui::C_DIM);
                ui::text(36, y, T(x.name), ok ? ui::C_TEXT : ui::C_DIM);
                y += 13;
            }
            if (tally_.continents.size() == 6) ui::text(140, ui::BODY_Y + 30, "WAC!", ui::C_OK, 2);
        } else if (page_ == 2) {
            snprintf(b, sizeof(b), "%u / 81", (unsigned)tally_.provinceSet.size());
            ui::text(6, y, b, ui::C_OK, 2);
            ui::text(110, y + 4, T("missing:"), ui::C_DIM);
            y += 20;
            std::string missing;
            for (int i = 0; i < awards::kProvinces; i++)
                if (!tally_.provinceSet.count(i)) missing += std::string(missing.empty() ? "" : ", ") + awards::provinces()[i];
            std::vector<std::string> lines = ui::wrapLines(missing, 38);
            int rows = (ui::H - ui::FOOTER_H - y) / 10;
            if (scroll_ > (int)lines.size() - rows) scroll_ = lines.size() > (size_t)rows ? (int)lines.size() - rows : 0;
            for (int i = 0; i < rows && scroll_ + i < (int)lines.size(); i++) ui::text(4, y + i * 10, lines[scroll_ + i], ui::C_TEXT);
        } else {
            snprintf(b, sizeof(b), "%u", (unsigned)tally_.grids.size());
            ui::text(6, y, b, ui::C_OK, 3);
            ui::text(70, y + 4, T("grid squares worked"), ui::C_TEXT);
            y += 32;
            drawBands(y, [&](const std::string& band) { return tally_.gridsByBand.count(band) ? tally_.gridsByBand.at(band).size() : 0; });
        }
        ui::footer(page_ == 2 ? T("TAB page  arrows scroll  ` back") : T("TAB page  ` back"));
    }

private:
    awards::Tally tally_;
    int page_ = 0, scroll_ = 0;
    bool pending_ = false;

    template <class F>
    void drawBands(int y, F count) {
        static const char* bands[] = {"160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "2m", "70cm"};
        for (int i = 0; i < 12; i++) {
            size_t n = count(bands[i]);
            int x = 6 + (i % 4) * 58, yy = y + (i / 4) * 12;
            ui::text(x, yy, bands[i], n ? ui::C_TEXT : ui::C_DIM);
            ui::text(x + 30, yy, std::to_string(n), n ? ui::C_OK : ui::C_DIM);
        }
    }
};
