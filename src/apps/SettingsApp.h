#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Ui.h"

// Edits the values in /HamOS/config.txt.
class SettingsApp : public App {
public:
    const char* name() const override { return "Settings"; }
    uint16_t color() const override { return 0x8410; }

    void onEnter() override {
        editing_ = false;
        status_.clear();
    }

    bool onBack() override {
        if (!editing_) return false;
        editing_ = false;   // cancel edit, stay in Settings
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        const Field& f = fields()[selected_];
        if (editing_) {
            if (k.enter) {
                config::set(f.key, input_.value);
                status_ = config::save() ? "Saved" : "No SD card: not saved";
                if (f.wifi) net::begin();
                editing_ = false;
            } else if (!input_.handle(k)) {
                return;
            }
            dirty = true;
        } else if (k.enter) {
            input_ = ui::TextInput();
            input_.value = config::get(f.key, f.def);
            input_.maxLen = f.maxLen;
            input_.upper = f.upper;
            editing_ = true;
            status_.clear();
            dirty = true;
        } else if (ui::listNav(k, selected_, (int)fields().size())) {
            dirty = true;
        }
    }

    void draw() override {
        if (editing_) {
            ui::clearBody();
            input_.draw(ui::BODY_Y + 20, fields()[selected_].label);
            ui::footer("ENTER save   ` cancel");
            return;
        }
        std::vector<std::string> rows;
        for (const Field& f : fields()) {
            std::string v = config::get(f.key, f.def);
            if (f.secret && !v.empty()) v = "********";
            rows.push_back(std::string(f.label) + ": " + (v.empty() ? "-" : v));
        }
        ui::list(rows, selected_, scroll_);
        ui::footer(status_.empty() ? "ENTER edit   ` back" : status_.c_str());
    }

private:
    struct Field {
        const char* key;
        const char* label;
        const char* def;
        size_t maxLen;
        bool upper, secret, wifi;
    };

    static const std::vector<Field>& fields() {
        static const std::vector<Field> f = {
            {"callsign", "Callsign", "", 10, true, false, false},
            {"grid", "Grid locator", "", 6, false, false, false},
            {"utc_offset", "UTC offset (h)", "3", 5, false, false, false},
            {"wifi_ssid", "WiFi SSID", "", 32, false, false, true},
            {"wifi_pass", "WiFi password", "", 63, false, true, true},
            {"cw_wpm", "CW speed WPM", "18", 2, false, false, false},
            {"cw_tone", "CW tone Hz", "700", 4, false, false, false},
            {"koch_level", "Koch level", "2", 2, false, false, false},
        };
        return f;
    }

    int selected_ = 0, scroll_ = 0;
    bool editing_ = false;
    ui::TextInput input_;
    std::string status_;
};
