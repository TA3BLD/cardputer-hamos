// The settings HamOS knows, shared by the Settings app and the USB link.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace settings {

enum { NONE = 0, WIFI = 1, MESH = 2, APRS = 4, RIG = 8, FT8 = 16, LANG = 32 };    // what restarts when the value changes

struct Field {
    const char* key;
    const char* label;
    const char* def;
    size_t maxLen;
    bool upper, secret;
    int apply;
};

inline const std::vector<Field>& fields() {
    static const std::vector<Field> f = {
            {"callsign", "Callsign", "", 10, true, false, settings::MESH | settings::APRS},
            {"grid", "Grid locator", "", 6, false, false, settings::NONE},
            {"utc_offset", "UTC offset (h)", "3", 5, false, false, settings::NONE},
            {"wifi_ssid", "WiFi SSID", "", 32, false, false, settings::WIFI},
            {"wifi_pass", "WiFi password", "", 63, false, true, settings::WIFI},
            {"web_on", "Web UI (0/1)", "0", 1, false, false, settings::NONE},
            {"web_pass", "Web UI password", "", 32, false, true, settings::NONE},
            {"lora_mode", "LoRa mode mesh/aprs", "mesh", 4, false, false, settings::MESH},
            {"mesh_on", "Mesh (0/1)", "1", 1, false, false, settings::MESH},
            {"mesh_region", "Mesh region", "EU_868", 8, true, false, settings::MESH},
            {"mesh_preset", "Mesh preset", "LongFast", 12, false, false, settings::MESH},
            {"mesh_channel", "Mesh channel", "", 12, false, false, settings::MESH},
            {"mesh_psk", "Mesh key (base64)", "AQ==", 44, false, true, settings::MESH},
            {"mesh_name", "Mesh long name", "", 36, false, false, settings::MESH},
            {"mesh_short", "Mesh short name", "", 4, false, false, settings::MESH},
            {"mesh_pos", "Share position 0/1", "0", 1, false, false, settings::NONE},
            {"mesh_beep", "Message beep 0/1", "1", 1, false, false, settings::NONE},
            {"lora_power", "LoRa power dBm", "22", 3, false, false, settings::MESH},
            {"lora_aprs_freq", "LoRa-APRS MHz", "433.775", 8, false, false, settings::MESH},
            {"lora_aprs_beacon", "LoRa beacon min", "0", 3, false, false, settings::NONE},
            {"lora_igate", "LoRa iGate 0/1", "0", 1, false, false, settings::NONE},
            {"aprs_on", "APRS-IS (0/1)", "0", 1, false, false, settings::APRS},
            {"aprs_ssid", "APRS SSID", "7", 2, false, false, settings::APRS},
            {"aprs_range", "APRS range km", "50", 4, false, false, settings::APRS},
            {"aprs_beacon", "APRS beacon 0/1", "0", 1, false, false, settings::NONE},
            {"aprs_comment", "APRS comment", "HamOS", 36, false, false, settings::NONE},
            {"dx_host", "DX cluster host", "dxc.ve7cc.net", 40, false, false, settings::NONE},
            {"dx_port", "DX cluster port", "23", 5, false, false, settings::NONE},
            {"dx_beep", "New DXCC beep 0/1", "1", 1, false, false, settings::NONE},
            {"rig_host", "Rig host (rigctld)", "", 40, false, false, settings::RIG},
            {"rig_port", "Rig port", "4532", 5, false, false, settings::RIG},
            {"ft8_dial", "FT8 dial MHz", "14.074", 8, false, false, settings::NONE},
            {"ft8_tx_hz", "FT8 TX audio Hz", "1500", 4, false, false, settings::NONE},
            {"wsjtx_on", "WSJT-X link 0/1", "0", 1, false, false, settings::FT8},
            {"wsjtx_port", "WSJT-X UDP port", "2237", 5, false, false, settings::FT8},
            {"pota_ref", "POTA park ref", "", 12, true, false, settings::NONE},
            {"contest", "Contest mode 0/1", "0", 1, false, false, settings::NONE},
            {"id_timer", "ID reminder min", "0", 3, false, false, settings::NONE},
            {"cw_wpm", "CW speed WPM", "18", 2, false, false, settings::NONE},
            {"cw_tone", "CW tone Hz", "700", 4, false, false, settings::NONE},
            {"cw_rx_tone", "CW decode tone Hz", "700", 4, false, false, settings::NONE},
            {"koch_level", "Koch level", "2", 2, false, false, settings::NONE},
            {"brightness", "Brightness 10-255", "160", 3, false, false, settings::NONE},
            {"dim_after", "Dim after s (0=no)", "60", 4, false, false, settings::NONE},
            {"lang", "Language tr/en", "en", 2, false, false, settings::LANG},
            {"update_check", "Update check 0/1", "1", 1, false, false, settings::NONE},
            {"update_repo", "Update repository", "TA3BLD/cardputer-hamos", 60, false, false, settings::NONE},
            {"update_token", "Update token", "", 100, false, true, settings::NONE},
    };
    return f;
}

inline const Field* find(const char* key) {
    for (const Field& f : fields())
        if (std::string(f.key) == key) return &f;
    return nullptr;
}

// Restarts what depends on a changed setting (defined in core/Settings.cpp).
void apply(int what);

}  // namespace settings
