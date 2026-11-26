#pragma once
#include <M5Cardputer.h>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Audio.h"
#include "core/Ble.h"
#include "core/Config.h"
#include "core/Gps.h"
#include "core/Lang.h"
#include "core/LoraAprs.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Storage.h"
#include "core/System.h"
#include "core/Ui.h"
#include "core/Weather.h"

// Checks every part once: SD card (write and read back), clock, WiFi, GPS,
// LoRa, radio, Bluetooth, sensor, battery, memory, microphone (1 s of sound
// level) and speaker (a beep: Y if heard). R runs it again.
class SelfTestApp : public App {
public:
    const char* name() const override { return T("Self test"); }
    uint16_t color() const override { return 0x2DE8; }

    void onEnter() override { start(); }
    void onExit() override {
        if (audio::capturing()) audio::stopCapture();
    }

    void onTick(uint32_t now) override {
        if (phase_ == RUN && !dirty) {
            runChecks();
            phase_ = MIC;
            micStart_ = now;
            micPeak_ = 0;
            audio::startCapture(8000);
            dirty = true;
        } else if (phase_ == MIC) {
            if (const int16_t* b = audio::capture())
                for (size_t i = 0; i < audio::BLOCK; i++) micPeak_ = std::max(micPeak_, abs((int)b[i]));
            if (now - micStart_ > 1000) {
                audio::stopCapture();
                int pct = micPeak_ * 100 / 32768;
                add(T("Microphone"), pct > 0 ? OK : FAIL, std::string(T("peak")) + " " + std::to_string(pct) + "%");
                M5Cardputer.Speaker.tone(1000, 400);
                phase_ = SPEAKER;
                dirty = true;
            }
        }
    }

    void onKey(const KeyEvent& k) override {
        if (phase_ == SPEAKER && (k.ch == 'y' || k.ch == 'Y' || k.ch == 'n' || k.ch == 'N')) {
            bool heard = k.ch == 'y' || k.ch == 'Y';
            add(T("Speaker"), heard ? OK : FAIL, heard ? T("heard") : T("not heard"));
            phase_ = DONE;
        } else if (k.ch == 'r' || k.ch == 'R') {
            start();
        } else if (!ui::listNav(k, selected_, (int)rows_.size())) {
            return;
        }
        dirty = true;
    }

    void draw() override {
        ui::clearBody();
        if (phase_ == RUN) {
            ui::textCentered(60, T("Checking..."), ui::C_DIM);
            ui::footer("");
            return;
        }
        std::vector<std::string> lines;
        std::vector<uint16_t> colors;
        int fails = 0;
        for (const Row& r : rows_) {
            const char* mark = r.state == OK ? "OK  " : r.state == FAIL ? "FAIL" : "--  ";
            char b[96];
            snprintf(b, sizeof(b), "%s %-11.11s %s", mark, r.name.c_str(), r.detail.c_str());
            lines.push_back(b);
            colors.push_back(r.state == OK ? ui::C_OK : r.state == FAIL ? ui::C_ERR : ui::C_DIM);
            fails += r.state == FAIL;
        }
        ui::list(lines, selected_, scroll_, &colors);
        if (phase_ == MIC) ui::footer(T("Listening for 1 s..."));
        else if (phase_ == SPEAKER) ui::footer(T("Did you hear a beep? Y / N"));
        else ui::footer(fails ? (std::to_string(fails) + " " + T("problems  R again")).c_str() : T("All fine  R again"));
    }

    enum State { OK, FAIL, OFF };
    struct Row {
        std::string name, detail;
        State state;
    };
    const std::vector<Row>& rows() const { return rows_; }
    bool finished() const { return phase_ == SPEAKER || phase_ == DONE; }

private:
    enum Phase { RUN, MIC, SPEAKER, DONE };
    Phase phase_ = RUN;
    std::vector<Row> rows_;
    int selected_ = 0, scroll_ = 0, micPeak_ = 0;
    uint32_t micStart_ = 0;

    void start() {
        rows_.clear();
        selected_ = scroll_ = 0;
        phase_ = RUN;
        dirty = true;
    }

    void add(const std::string& n, State s, const std::string& d) { rows_.push_back({n, d, s}); }

    static std::string kb(size_t b) { return std::to_string(b / 1024) + " KB"; }

    void runChecks() {
        // SD card: write, read back, remove
        if (!storage::ready()) {
            add(T("SD card"), FAIL, T("not found"));
        } else {
            bool ok = storage::writeFile("/HamOS/selftest.tmp", "HamOS 73");
            std::string back;
            ok = ok && storage::readFile("/HamOS/selftest.tmp", back) && back == "HamOS 73";
            storage::remove("/HamOS/selftest.tmp");
            add(T("SD card"), ok ? OK : FAIL, ok ? T("read and write") : T("write failed"));
        }
        add(T("Clock"), net::timeValid() ? OK : FAIL, net::timeValid() ? T("time known") : T("WiFi or GPS needed"));
        if (!net::configured()) add("WiFi", OFF, T("not set up"));
        else add("WiFi", net::connected() ? OK : FAIL, net::connected() ? net::address() : T("not connected"));
        if (!gps::present()) add("GPS", FAIL, T("no data (cap attached?)"));
        else add("GPS", gps::hasFix() ? OK : OFF, gps::hasFix() ? gps::grid() : T("no fix yet (open sky)"));
        if (mesh::ready()) add("LoRa", OK, "Meshtastic");
        else if (loraaprs::ready()) add("LoRa", OK, "LoRa-APRS");
        else add("LoRa", FAIL, mesh::status());
        if (!*rig::typeName()) add(T("Radio"), OFF, T("not set up"));
        else add(T("Radio"), rig::connected() ? OK : FAIL, rig::status());
        bool btWanted = config::get("mesh_ble", "0") == "1" || config::get("rig_type") == "ic705";
        add("Bluetooth", btWanted ? OK : OFF, btWanted ? (ble::meshConnected() ? T("app connected") : std::string(T("visible as")) + " " + ble::meshName()) : T("off"));
        if (config::get("grove_io") == "wx") add("BME280", weather::present() ? OK : FAIL, weather::status());
        int bat = M5Cardputer.Power.getBatteryLevel();
        add(T("Battery"), bat > 15 ? OK : FAIL, std::to_string(bat) + "%");
        size_t freeB = sys::freeHeap(), big = sys::largestBlock();
        add(T("Memory"), big > 100 * 1024 ? OK : FAIL, kb(freeB) + " " + T("free") + ", " + kb(big) + " " + T("block"));
        add(T("Memory min"), sys::minFreeHeap() > 40 * 1024 ? OK : FAIL, kb(sys::minFreeHeap()));
        add(T("Screen buffer"), ui::buffered() ? OK : FAIL, ui::buffered() ? T("on (no flicker)") : T("off: not enough memory"));
        add(T("Last start"), sys::resetWasFault() ? FAIL : OK, T(sys::resetReason()));
        add("APRS-IS", config::get("aprs_on", "0") != "1" ? OFF : aprsis::online() ? OK : FAIL, aprsis::status());
    }
};
