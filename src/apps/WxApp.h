#pragma once
#include <cstdio>
#include <string>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/LoraAprs.h"
#include "core/Ui.h"
#include "core/Weather.h"

// Weather station (BME280 on the Grove port) and the LoRa-APRS digipeater.
class WxApp : public App {
public:
    const char* name() const override { return T("WX / Digi"); }
    uint16_t color() const override { return 0x5D1F; }

    void onTick(uint32_t now) override {
        if (now - lastMs_ > 2000) { lastMs_ = now; dirty = true; }
    }

    void onKey(const KeyEvent& k) override {
        if (k.ch == 'b' || k.ch == 'B') {
            std::string e;
            message_ = weather::beaconNow(e) ? T("Weather report sent") : e;
            dirty = true;
        }
        if (k.ch == 'd' || k.ch == 'D') {
            std::string d = config::get("lora_digi", "0");
            config::set("lora_digi", d == "0" ? "1" : d == "1" ? "2" : "0");
            config::save();
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        int y = ui::BODY_Y + 4;
        if (weather::valid()) {
            char b[32];
            snprintf(b, sizeof(b), "%.1f C", weather::celsius());
            ui::text(6, y, b, ui::C_TEXT, 2.5);
            snprintf(b, sizeof(b), "%.0f %%", weather::humidity());
            ui::text(150, y + 4, b, ui::C_OK, 2);
            y += 26;
            snprintf(b, sizeof(b), "%.1f hPa", weather::seaLevelHpa());
            ui::text(6, y, b, ui::C_NOTICE, 2);
            snprintf(b, sizeof(b), "(%.1f)", weather::pressureHpa());
            ui::text(150, y + 4, b, ui::C_DIM);
            y += 20;
        } else {
            ui::text(6, y + 6, weather::status(), ui::C_WARN);
            y += 30;
        }
        int wxMin = atoi(config::get("wx_beacon", "0").c_str());
        std::string beacon = wxMin > 0 ? std::string(T("WX beacon every")) + " " + std::to_string(wxMin) + " min" : T("WX beacon off");
        ui::text(6, y, beacon, ui::C_DIM);
        y += 14;
        std::string d = config::get("lora_digi", "0");
        const char* mode = d == "1" ? T("Digi: WIDE1-1 fill-in") : d == "2" ? T("Digi: WIDE1 + WIDE2") : T("Digi: off");
        ui::text(6, y, mode, d == "0" ? ui::C_DIM : ui::C_OK);
        if (d != "0") ui::text(170, y, std::to_string(loraaprs::digipeated()) + " " + T("rep."), ui::C_DIM);
        y += 12;
        if (d != "0" && !loraaprs::ready()) ui::text(6, y, T("(LoRa mode must be aprs)"), ui::C_WARN);
        else if (!message_.empty()) ui::text(6, y, message_, ui::C_OK);
        ui::footer(T("B send WX now  D digi mode"));
    }

private:
    uint32_t lastMs_ = 0;
    std::string message_;
};
