#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "core/Ui.h"

// First start: language, callsign, locator, WiFi, LoRa use - one question per
// screen. Opens by itself while no callsign is set; can be run again later.
class SetupApp : public App {
public:
    const char* name() const override { return T("Setup wizard"); }
    uint16_t color() const override { return 0x47E0; }

    bool finished = false;              // main.cpp goes to the launcher

    void onEnter() override {
        step_ = 0;
        finished = false;
        load();
    }

    bool onBack() override {
        if (step_ > 0) { step_--; load(); dirty = true; return true; }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        const Step& s = steps()[step_];
        dirty = true;
        if (k.enter) {
            store();
            if (step_ + 1 < (int)steps().size()) {
                step_++;
                load();
            } else {
                config::set("setup_done", "1");
                config::save();
                settings::apply(settings::WIFI | settings::MESH | settings::APRS | settings::RIG | settings::LANG);
                finished = true;
            }
            return;
        }
        if (s.choices) {
            int n = 0;
            while (s.choices[n]) n++;
            if (k.left() || k.right() || k.ch == ' ') choice_ = (choice_ + (k.left() ? n - 1 : 1)) % n;
            return;
        }
        if ((k.ch == 'g' || k.ch == 'G') && std::string(s.key) == "grid" && !gps::grid().empty() && input_.value.empty()) {
            input_.value = gps::grid();
            return;
        }
        input_.handle(k);
    }

    void draw() override {
        const Step& s = steps()[step_];
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, std::string(T("Setup")) + "  " + std::to_string(step_ + 1) + "/" + std::to_string(steps().size()),
                 ui::C_NOTICE);
        int y = ui::BODY_Y + 18;
        ui::text(6, y, T(s.question), ui::C_TEXT, 1.5);
        y += 22;
        if (s.choices) {
            int n = 0;
            while (s.choices[n]) n++;
            int x = 6;
            for (int i = 0; i < n; i++) {
                std::string c = T(s.choices[i]);
                int w = (int)ui::glyphCount(c) * 6 + 10;
                ui::fill(x, y - 2, w, 14, i == choice_ ? ui::C_SEL : ui::C_FOOT);
                ui::textOn(x + 5, y + 1, c, ui::C_TEXT, i == choice_ ? ui::C_SEL : ui::C_FOOT);
                x += w + 6;
            }
        } else {
            ui::text(6, y, input_.value + "_", ui::C_OK, 1.5);
        }
        y += 24;
        for (const std::string& l : ui::wrapLines(T(s.help), 38)) {
            ui::text(6, y, l, ui::C_DIM);
            y += 10;
        }
        ui::footer(s.choices ? T("arrows choose  ENTER next  ` back") : T("type, ENTER next  ` back"));
    }

private:
    struct Step {
        const char* key;
        const char* question;
        const char* help;
        const char* const* choices;     // nullptr: free text
        bool upper;
    };
    static const std::vector<Step>& steps() {
        static const char* const langs[] = {"Türkçe", "English", nullptr};
        static const char* const lora[] = {"Meshtastic", "LoRa-APRS", "off", nullptr};
        static const char* const regions[] = {"EU_868", "EU_433", "US", nullptr};
        static const std::vector<Step> s = {
                {"lang", "Language", "Can be changed later in Settings.", langs, false},
                {"callsign", "Your callsign", "Used for the log, APRS, FT8, the mesh name.", nullptr, true},
                {"grid", "Your locator", "6 characters, e.g. KM38nk. G takes it from the GPS when it has a fix.", nullptr, false},
                {"wifi_ssid", "WiFi name", "Empty: no WiFi (clock from GPS then).", nullptr, false},
                {"wifi_pass", "WiFi password", "Stored on the SD card.", nullptr, false},
                {"lora_mode", "LoRa cap used for", "Meshtastic chat, or LoRa-APRS on 433.775 MHz.", lora, false},
                {"mesh_region", "LoRa region", "EU_868 in Europe and Turkey; EU_433 for the 433 MHz band.", regions, false},
                {"done", "Ready!", "ENTER starts HamOS. This wizard is in Tools; every value is in Settings.", nullptr, false},
        };
        return s;
    }

    int step_ = 0, choice_ = 0;
    ui::TextInput input_;

    void load() {
        const Step& s = steps()[step_];
        input_ = ui::TextInput();
        input_.upper = s.upper;
        input_.maxLen = 40;
        std::string key = s.key;
        if (key == "done") return;
        std::string v = config::get(key);
        if (key == "lang") choice_ = config::get("lang", "tr") == "en" ? 1 : 0;
        else if (key == "lora_mode") choice_ = config::get("mesh_on", "1") == "0" ? 2 : config::get("lora_mode", "mesh") == "aprs" ? 1 : 0;
        else if (key == "mesh_region") choice_ = v == "EU_433" ? 1 : v == "US" ? 2 : 0;
        else input_.value = v;
    }

    void store() {
        std::string key = steps()[step_].key;
        if (key == "done") return;
        if (key == "lang") {
            config::set("lang", choice_ == 1 ? "en" : "tr");
            settings::apply(settings::LANG);
        } else if (key == "lora_mode") {
            config::set("lora_mode", choice_ == 1 ? "aprs" : "mesh");
            config::set("mesh_on", choice_ == 2 ? "0" : "1");
        } else if (key == "mesh_region") {
            static const char* r[] = {"EU_868", "EU_433", "US"};
            config::set("mesh_region", r[choice_]);
        } else if (input_.value.empty()) {
            config::erase(key);
        } else {
            config::set(key, input_.value);
        }
    }
};
