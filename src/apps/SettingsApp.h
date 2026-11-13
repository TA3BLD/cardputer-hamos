#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Config.h"
#include "core/Mesh.h"
#include "core/Rig.h"
#include "core/Net.h"
#include "core/Ui.h"
#include "core/Web.h"
#include "core/Wsjtx.h"

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
                if (input_.value.empty()) config::erase(f.key);     // empty = back to the default
                else config::set(f.key, input_.value);
                status_ = config::save() ? "Saved" : "No SD card: not saved";
                if (f.apply & WIFI) net::begin();
                if (f.apply & MESH) mesh::begin();
                if (f.apply & APRS) aprsis::begin();
                if (f.apply & RIG) rig::begin();
                if (f.apply & FT8) wsjtxlink::begin();
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
        std::string hint = !status_.empty() ? status_ : web::running() ? web::address() : "ENTER edit   ` back";
        ui::footer(hint.c_str());
    }

private:
    enum { NONE = 0, WIFI = 1, MESH = 2, APRS = 4, RIG = 8, FT8 = 16 };    // what restarts when the value changes
    struct Field {
        const char* key;
        const char* label;
        const char* def;
        size_t maxLen;
        bool upper, secret;
        int apply;
    };

    static const std::vector<Field>& fields() {
        static const std::vector<Field> f = {
            {"callsign", "Callsign", "", 10, true, false, MESH | APRS},
            {"grid", "Grid locator", "", 6, false, false, NONE},
            {"utc_offset", "UTC offset (h)", "3", 5, false, false, NONE},
            {"wifi_ssid", "WiFi SSID", "", 32, false, false, WIFI},
            {"wifi_pass", "WiFi password", "", 63, false, true, WIFI},
            {"web_on", "Web UI (0/1)", "0", 1, false, false, NONE},
            {"web_pass", "Web UI password", "", 32, false, true, NONE},
            {"mesh_on", "Mesh (0/1)", "1", 1, false, false, MESH},
            {"mesh_region", "Mesh region", "EU_868", 8, true, false, MESH},
            {"mesh_preset", "Mesh preset", "LongFast", 12, false, false, MESH},
            {"mesh_channel", "Mesh channel", "", 12, false, false, MESH},
            {"mesh_psk", "Mesh key (base64)", "AQ==", 44, false, true, MESH},
            {"mesh_name", "Mesh long name", "", 36, false, false, MESH},
            {"mesh_short", "Mesh short name", "", 4, false, false, MESH},
            {"mesh_pos", "Share position 0/1", "0", 1, false, false, NONE},
            {"mesh_beep", "Message beep 0/1", "1", 1, false, false, NONE},
            {"lora_power", "LoRa power dBm", "22", 3, false, false, MESH},
            {"aprs_on", "APRS-IS (0/1)", "0", 1, false, false, APRS},
            {"aprs_ssid", "APRS SSID", "7", 2, false, false, APRS},
            {"aprs_range", "APRS range km", "50", 4, false, false, APRS},
            {"aprs_beacon", "APRS beacon 0/1", "0", 1, false, false, NONE},
            {"aprs_comment", "APRS comment", "HamOS", 36, false, false, NONE},
            {"dx_host", "DX cluster host", "dxc.ve7cc.net", 40, false, false, NONE},
            {"dx_port", "DX cluster port", "23", 5, false, false, NONE},
            {"dx_beep", "New DXCC beep 0/1", "1", 1, false, false, NONE},
            {"rig_host", "Rig host (rigctld)", "", 40, false, false, RIG},
            {"rig_port", "Rig port", "4532", 5, false, false, RIG},
            {"ft8_dial", "FT8 dial MHz", "14.074", 8, false, false, NONE},
            {"ft8_tx_hz", "FT8 TX audio Hz", "1500", 4, false, false, NONE},
            {"wsjtx_on", "WSJT-X link 0/1", "0", 1, false, false, FT8},
            {"wsjtx_port", "WSJT-X UDP port", "2237", 5, false, false, FT8},
            {"pota_ref", "POTA park ref", "", 12, true, false, NONE},
            {"contest", "Contest mode 0/1", "0", 1, false, false, NONE},
            {"id_timer", "ID reminder min", "0", 3, false, false, NONE},
            {"cw_wpm", "CW speed WPM", "18", 2, false, false, NONE},
            {"cw_tone", "CW tone Hz", "700", 4, false, false, NONE},
            {"cw_rx_tone", "CW decode tone Hz", "700", 4, false, false, NONE},
            {"koch_level", "Koch level", "2", 2, false, false, NONE},
            {"brightness", "Brightness 10-255", "160", 3, false, false, NONE},
            {"dim_after", "Dim after s (0=no)", "60", 4, false, false, NONE},
        };
        return f;
    }

    int selected_ = 0, scroll_ = 0;
    bool editing_ = false;
    ui::TextInput input_;
    std::string status_;
};
