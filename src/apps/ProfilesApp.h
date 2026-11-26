#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Settings.h"
#include "core/Storage.h"
#include "core/Ui.h"

// Profiles: sets of settings switched with one key - Home, Field (POTA/SOTA),
// Emergency. ENTER applies one (after Y), S stores the current settings in it.
// Files: /HamOS/profiles/<n>.txt with "key=value" lines.
class ProfilesApp : public App {
public:
    const char* name() const override { return T("Profiles"); }
    uint16_t color() const override { return 0xB5B6; }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (confirm_) {
            if (k.ch == 'y' || k.ch == 'Y') {
                apply(selected_);
                message_ = std::string(T("Profile active")) + ": " + T(kNames[selected_]);
            }
            confirm_ = false;
            return;
        }
        if (ui::listNav(k, selected_, 3)) return;
        if (k.enter) confirm_ = true;
        if (k.ch == 's' || k.ch == 'S') message_ = save(selected_) ? T("Current settings stored in the profile") : T("Could not write to the SD card");
    }

    void draw() override {
        ui::clearBody();
        std::string active = config::get("profile");
        int y = ui::BODY_Y + 4;
        for (int i = 0; i < 3; i++) {
            bool sel = i == selected_;
            if (sel) ui::fill(0, y - 2, ui::W, 16, ui::C_SEL);
            ui::text(8, y, T(kNames[i]), ui::C_TEXT, 1.5);
            if (active == std::to_string(i)) ui::text(ui::W - 60, y + 3, T("active"), sel ? ui::C_TEXT : ui::C_OK);
            y += 18;
        }
        y += 2;
        for (const std::string& l : ui::wrapLines(T(kHelp[selected_]), 38)) {
            ui::text(4, y, l, ui::C_DIM);
            y += 10;
        }
        if (!message_.empty()) ui::text(4, ui::H - ui::FOOTER_H - 11, message_, ui::C_OK);
        ui::footer(confirm_ ? T("Switch to this profile? Y") : T("ENTER use  S store current here"));
    }

    // The settings a profile holds.
    static const std::vector<const char*>& keys() {
        static const std::vector<const char*> k = {"brightness", "dim_after", "lora_mode", "mesh_on", "mesh_ble", "aprs_on",
                                                   "aprs_beacon", "lora_aprs_beacon", "lora_digi", "rig_type", "rig_host",
                                                   "grove_io", "pota_ref", "sota_ref", "contest", "wx_beacon", "psk_report",
                                                   "volume", "update_check"};
        return k;
    }

    static void apply(int i) {
        std::string text;
        if (!storage::readFile(path(i).c_str(), text)) text = kDefaults[i];
        size_t a = 0;
        while (a < text.size()) {
            size_t e = text.find('\n', a);
            std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
            a = e == std::string::npos ? text.size() : e + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            if (value.empty()) config::erase(key);
            else config::set(key, value);
        }
        config::set("profile", std::to_string(i));
        config::save();
        settings::apply(settings::WIFI | settings::MESH | settings::APRS | settings::RIG);
    }

private:
    static constexpr const char* kNames[3] = {"Home", "Field", "Emergency"};
    static constexpr const char* kHelp[3] = {
            "Normal use: bright screen, APRS-IS on, no LoRa beacon.",
            "Portable / POTA / SOTA: dimmer screen, LoRa-APRS beacon every 10 min, PSK Reporter on.",
            "Long battery life: dim screen, mesh with the Meshtastic app over Bluetooth, nothing else."};
    static constexpr const char* kDefaults[3] = {
            "brightness=160\ndim_after=60\nlora_mode=mesh\nmesh_on=1\nmesh_ble=0\naprs_on=1\nlora_aprs_beacon=0\npsk_report=0\n",
            "brightness=90\ndim_after=20\nlora_mode=aprs\nmesh_on=1\nmesh_ble=0\naprs_on=0\nlora_aprs_beacon=10\npsk_report=1\n",
            "brightness=40\ndim_after=15\nlora_mode=mesh\nmesh_on=1\nmesh_ble=1\naprs_on=0\nlora_aprs_beacon=0\npsk_report=0\n"
            "update_check=0\n"};

    int selected_ = 0;
    bool confirm_ = false;
    std::string message_;

    static std::string path(int i) { return "/HamOS/profiles/" + std::to_string(i + 1) + ".txt"; }

    static bool save(int i) {
        std::string text;
        for (const char* k : keys()) text += std::string(k) + "=" + config::get(k) + "\n";
        storage::makeDir("/HamOS/profiles");
        return storage::writeFile(path(i).c_str(), text);
    }
};
