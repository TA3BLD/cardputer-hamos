#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/App.h"
#include "core/Ble.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Rig.h"
#include "core/Ui.h"
#include "lib/Adif.h"

// The connected transceiver: frequency and mode, tuning (arrows: up/down
// 1 kHz, left/right 100 Hz; Fn+arrows 10x), band presets B, mode M, PTT test P.
class RadioApp : public App {
public:
    const char* name() const override { return T("Radio"); }
    uint16_t color() const override { return 0xC618; }

    void onTick(uint32_t now) override {
        if (now - lastMs_ > 500) {
            lastMs_ = now;
            std::string s = rig::status();
            if (s != shown_) { shown_ = s; dirty = true; }
        }
    }

    void onExit() override {
        if (ptt_) rig::ptt(false);
        ptt_ = false;
    }

    void onKey(const KeyEvent& k) override {
        double f = atof(rig::freqMhz().c_str());
        if (!rig::connected() || f <= 0) {
            if (k.ch == 'p' || k.ch == 'P') togglePtt();
            return;
        }
        double step = 0;
        if (k.fnUp() || k.ch == ';') step = 0.001;
        else if (k.fnDown() || k.ch == '.') step = -0.001;
        else if (k.fnRight() || k.ch == '/') step = 0.0001;
        else if (k.fnLeft() || k.ch == ',') step = -0.0001;
        if (step != 0) {
            if (k.fn) step *= 10;
            rig::tune(f + step, "");
            return;
        }
        if (k.ch == 'b' || k.ch == 'B') {
            static const double bands[] = {1.840, 3.573, 7.074, 10.136, 14.074, 18.100, 21.074, 24.915, 28.074, 50.313, 144.174};
            int n = sizeof(bands) / sizeof(bands[0]), i = 0;
            while (i < n && bands[i] <= f + 0.5) i++;
            rig::tune(bands[i % n], "FT8");
            return;
        }
        if (k.ch == 'm' || k.ch == 'M') {
            static const char* modes[] = {"SSB", "CW", "FT8", "FM", "AM"};
            modeIndex_ = (modeIndex_ + 1) % 5;
            rig::tune(f, modes[modeIndex_]);
            return;
        }
        if (k.ch == 'p' || k.ch == 'P') togglePtt();
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, *rig::typeName() ? rig::typeName() : T("No radio set"), ui::C_NOTICE);
        if (rig::connected()) {
            double f = atof(rig::freqMhz().c_str());
            char b[24];
            snprintf(b, sizeof(b), "%.4f", f);
            ui::textCentered(ui::BODY_Y + 24, b, ui::C_TEXT, 3);
            ui::textCentered(ui::BODY_Y + 54, rig::mode() + "  " + adif::band(f), ui::C_OK, 2);
            if (ptt_) ui::text(ui::W - 40, ui::BODY_Y + 2, "TX", ui::C_ERR, 1.5);
            ui::footer(T("arrows tune  B band  M mode  P PTT"));
        } else {
            int y = ui::BODY_Y + 30;
            for (const std::string& l : ui::wrapLines(rig::status(), 34)) {
                ui::textCentered(y, l, ui::C_WARN);
                y += 12;
            }
            ui::textCentered(y + 10, T("Settings: Rig type, see Guide 14"), ui::C_DIM);
            ui::footer(T("` back"));
        }
    }

private:
    uint32_t lastMs_ = 0;
    std::string shown_;
    int modeIndex_ = 0;
    bool ptt_ = false;

    void togglePtt() {
        ptt_ = !ptt_;
        if (!rig::ptt(ptt_)) ptt_ = false;
        dirty = true;
    }
};
