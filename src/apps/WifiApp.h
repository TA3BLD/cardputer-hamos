#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Net.h"
#include "core/Settings.h"
#include "core/Ui.h"
#include "core/Web.h"

// WiFi: what the device is connected to (network, signal, IP address, the
// http://hamos.local name for the web interface), a scan for networks to
// pick from instead of typing the name, and the web interface on/off.
class WifiApp : public App {
public:
    const char* name() const override { return "WiFi"; }
    uint16_t color() const override { return 0x2D7F; }

    void onEnter() override {
        page_ = STATUS;
        message_.clear();
    }

    // Opens straight on the network list (from Settings).
    void openScan() {
        page_ = LIST;
        selected_ = scroll_ = 0;
        net::scanStart();
    }

    bool onBack() override {
        if (page_ == PASSWORD) {
            page_ = LIST;
        } else if (page_ == LIST) {
            page_ = STATUS;
        } else {
            return false;
        }
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (page_ == PASSWORD) {
            if (k.enter) {
                join(chosen_, input_.value);
            } else if (!input_.handle(k)) {
                return;
            }
            dirty = true;
            return;
        }
        if (page_ == LIST) {
            const std::vector<net::Network>& list = net::scanResults();
            if (k.ch == 's' || k.ch == 'S') {
                net::scanStart();
            } else if (k.enter && !list.empty() && !net::scanning()) {
                const net::Network& w = list[(size_t)selected_];
                chosen_ = w.ssid;
                if (w.open) {
                    join(w.ssid, "");
                } else {
                    input_ = ui::TextInput();
                    input_.maxLen = 63;
                    // The saved password when it is the same network again.
                    if (config::get("wifi_ssid") == w.ssid) input_.value = config::get("wifi_pass");
                    page_ = PASSWORD;
                }
            } else if (!ui::listNav(k, selected_, (int)list.size())) {
                return;
            }
            dirty = true;
            return;
        }
        // status page
        if (k.ch == 's' || k.ch == 'S' || k.enter) {
            openScan();
        } else if (k.ch == 'w' || k.ch == 'W') {
            bool on = config::get("web_on", "0") != "1";
            config::set("web_on", on ? "1" : "0");
            config::save();
            message_ = on ? T("Web UI on") : T("Web UI off");
        } else if (k.ch == 'r' || k.ch == 'R') {
            net::begin();
            message_ = T("Connecting...");
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        bool scanning = net::scanning();
        if (scanning != wasScanning_) {
            wasScanning_ = scanning;
            selected_ = scroll_ = 0;
            dirty = true;
        }
        if (now - lastMs_ >= 1000) {        // signal and connection change by themselves
            lastMs_ = now;
            if (page_ == STATUS) dirty = true;
        }
    }

    void draw() override {
        if (page_ == PASSWORD) drawPassword();
        else if (page_ == LIST) drawList();
        else drawStatus();
    }

private:
    enum { STATUS, LIST, PASSWORD };

    void join(const std::string& ssid, const std::string& pass) {
        config::set("wifi_ssid", ssid);
        if (pass.empty()) config::erase("wifi_pass");
        else config::set("wifi_pass", pass);
        bool saved = config::save();
        settings::apply(settings::WIFI);
        message_ = saved ? std::string(T("Connecting to")) + " " + ssid : T("No SD card: not saved");
        page_ = STATUS;
    }

    static std::string bars(int rssi) {
        int n = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
        return std::string((size_t)n, '|') + std::string((size_t)(4 - n), '.');
    }

    void row(int& y, const char* label, const std::string& value, uint16_t color = ui::C_TEXT) {
        ui::text(6, y, label, ui::C_DIM);
        ui::text(72, y, value, color);
        y += 12;
    }

    void drawStatus() {
        ui::clearBody();
        int y = ui::BODY_Y + 4;
        std::string saved = config::get("wifi_ssid");
        if (net::connected()) {
            char sig[32];
            snprintf(sig, sizeof(sig), "%d dBm %s", net::rssi(), bars(net::rssi()).c_str());
            row(y, T("Network"), ui::ascii(net::ssid()), ui::C_OK);
            row(y, T("Signal"), sig);
            row(y, T("IP"), net::address(), ui::C_NOTICE);
            row(y, T("Name"), net::hostname() + ".local");
        } else {
            row(y, T("Network"), saved.empty() ? T("not set") : ui::ascii(saved) + " - " + T("not connected"), ui::C_WARN);
            y += 24;
        }
        bool webOn = config::get("web_on", "0") == "1";
        if (webOn && web::running()) {
            row(y, T("Web UI"), web::address(), ui::C_OK);
            row(y, "", "http://" + net::hostname() + ".local", ui::C_OK);
        } else {
            row(y, T("Web UI"), webOn ? T("on, waiting for WiFi") : T("off (W turns it on)"), webOn ? ui::C_WARN : ui::C_DIM);
            if (webOn && !config::get("web_pass").empty()) row(y, "", T("user: hamos"), ui::C_DIM);
        }
        std::string hint = !message_.empty() ? message_ : T("S scan  W web UI  R reconnect");
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.c_str());
    }

    void drawList() {
        const std::vector<net::Network>& list = net::scanResults();
        if (net::scanning() || list.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 40, net::scanning() ? T("Looking for networks...") : T("No networks found"),
                             ui::C_DIM, 1.5);
            ui::footer(net::scanning() ? T("` back") : T("S scan again   ` back"));
            return;
        }
        std::vector<std::string> rows;
        std::vector<uint16_t> colors;
        std::string current = config::get("wifi_ssid");
        for (const net::Network& w : list) {
            char line[64];
            std::string name = ui::ascii(w.ssid);
            if (name.size() > 22) name = name.substr(0, 21) + "~";
            snprintf(line, sizeof(line), "%-22s %s %4d %s", name.c_str(), bars(w.rssi).c_str(), w.rssi, w.open ? T("open") : "");
            rows.push_back(line);
            colors.push_back(w.ssid == current ? ui::C_OK : ui::C_TEXT);
        }
        if (selected_ >= (int)rows.size()) selected_ = (int)rows.size() - 1;
        ui::list(rows, selected_, scroll_, &colors);
        ui::footer(T("ENTER join   S scan again   ` back"));
    }

    void drawPassword() {
        ui::clearBody();
        ui::text(6, ui::BODY_Y + 4, ui::ascii(chosen_), ui::C_NOTICE, 1.5);
        input_.draw(ui::BODY_Y + 26, T("WiFi password"));
        ui::footer(T("ENTER connect   ` cancel"));
    }

    int page_ = STATUS, selected_ = 0, scroll_ = 0;
    bool wasScanning_ = false;
    uint32_t lastMs_ = 0;
    std::string chosen_, message_;
    ui::TextInput input_;
};
