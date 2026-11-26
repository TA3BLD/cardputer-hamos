#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <M5Cardputer.h>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Config.h"
#include "core/Emergency.h"
#include "core/Gps.h"
#include "core/Lang.h"
#include "core/LoraAprs.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Station.h"
#include "core/Ui.h"

// Emergency: SOS, status messages, safety timer, siren, strobe, battery saver,
// alerts. Opens from any screen by holding the G0 side button for 2 seconds.
// TAB: SOS / Messages / Alerts / Info.
class EmergencyApp : public App {
public:
    const char* name() const override { return T("Emergency"); }
    uint16_t color() const override { return 0xF800; }

    void showAlerts() {
        page_ = ALERTS;
        dirty = true;
    }

    void onTick(uint32_t now) override {
        using emergency::State;
        bool active = emergency::state() != State::Idle || emergency::timerMinutes() > 0;
        if ((active && now - lastMs_ > 250) || now - lastMs_ > 2000) {
            lastMs_ = now;
            dirty = true;
        }
    }

    bool onBack() override {
        if (confirmStop_) { confirmStop_ = false; dirty = true; return true; }
        if (typing_) { typing_ = false; dirty = true; return true; }
        return false;                       // an active SOS goes on in the background
    }

    void onKey(const KeyEvent& k) override {
        using emergency::State;
        dirty = true;
        if (emergency::state() == State::Active) {
            if (confirmStop_) {
                if (k.ch == 'y' || k.ch == 'Y') emergency::stop();
                confirmStop_ = false;
                return;
            }
            if (k.ch == 'x' || k.ch == 'X') confirmStop_ = true;
            else if (k.ch == 'a' || k.ch == 'A') emergency::setSiren(!emergency::siren());
            else if (k.ch == 'l' || k.ch == 'L') emergency::setStrobe(!emergency::strobe());
            else if (k.ch == 'p' || k.ch == 'P') emergency::setSaver(!emergency::saver());
            else if (k.tab) page_ = page_ == ALERTS ? SOS : ALERTS;
            return;
        }
        if (typing_) {                          // optional note for the SOS
            if (k.enter) {
                typing_ = false;
                emergency::arm(note_.value);
            } else {
                note_.handle(k);
            }
            return;
        }
        if (k.tab) { page_ = (Page)((page_ + 1) % PAGES); return; }
        if (page_ == SOS) {
            if (k.ch == 's' || k.ch == 'S') emergency::arm("");
            else if (k.ch == 'n' || k.ch == 'N') {
                typing_ = true;
                note_.value.clear();
                note_.maxLen = 60;
            } else if (k.ch == 't' || k.ch == 'T') {
                static const int steps[] = {0, 30, 60, 120, 240};
                int cur = emergency::timerMinutes(), i = 0;
                while (i < 5 && steps[i] != cur) i++;
                emergency::setTimer(steps[(i + 1) % 5]);
            } else if (k.ch == 'a' || k.ch == 'A') emergency::setSiren(!emergency::siren());
            else if (k.ch == 'l' || k.ch == 'L') emergency::setStrobe(!emergency::strobe());
            else if (k.ch == 'p' || k.ch == 'P') emergency::setSaver(!emergency::saver());
        } else if (page_ == MESSAGES && k.ch >= '1' && k.ch <= '6') {
            std::string via;
            message_ = emergency::sendPreset(k.ch - '1', via) ? std::string(T("Sent via")) + " " + via : T("Nothing could send: no mesh, LoRa-APRS or internet");
        } else if (page_ == ALERTS) {
            ui::listNav(k, selected_, (int)emergency::alerts().size());
        }
    }

    void draw() override {
        using emergency::State;
        if (emergency::state() == State::Countdown) { drawCountdown(); return; }
        if (emergency::state() == State::Active && page_ != ALERTS) { drawActive(); return; }
        ui::clearBody();
        static const char* titles[] = {"SOS", "Status messages", "Alerts and replies", "Emergency info"};
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_ERR);
        ui::textOver(4, ui::BODY_Y + 2, T(titles[page_]), ui::C_TEXT);
        ui::textOver(ui::W - 30, ui::BODY_Y + 2, std::to_string(page_ + 1) + "/4", ui::C_TEXT);
        if (page_ == SOS) drawSos();
        else if (page_ == MESSAGES) drawMessages();
        else if (page_ == ALERTS) drawAlerts();
        else drawInfo();
    }

private:
    enum Page { SOS, MESSAGES, ALERTS, INFO, PAGES };
    Page page_ = SOS;
    int selected_ = 0, scroll_ = 0;
    bool confirmStop_ = false, typing_ = false;
    ui::TextInput note_;
    std::string message_;
    uint32_t lastMs_ = 0;

    static const int kTop = ui::BODY_Y + 14;

    // White text straight onto the red areas (ui::text paints a black background).
    static void overText(int x, int y, const std::string& s, float size) { ui::textOver(x, y, s, ui::C_TEXT, size); }
    static void overCentered(int cx, int y, const std::string& s, float size) {
        int w = (int)(ui::glyphCount(s) * 6 * size);
        overText(cx - w / 2, y, s, size);
    }

    // What can carry a call for help right now.
    void drawLinks(int y) {
        struct L { const char* name; bool ok; };
        L links[] = {{"Mesh", mesh::ready()}, {"LoRa-APRS", loraaprs::ready()}, {"APRS-IS", aprsis::online()}, {"GPS", gps::hasFix()}};
        int x = 4;
        for (const L& l : links) {
            ui::text(x, y, l.name, l.ok ? ui::C_OK : ui::C_DIM);
            x += (int)ui::glyphCount(l.name) * 6 + 10;
        }
        char b[24];
        snprintf(b, sizeof(b), "%d%%", M5Cardputer.Power.getBatteryLevel());
        ui::text(ui::W - 28, y, b, ui::C_TEXT);
    }

    void drawSos() {
        int y = kTop;
        if (typing_) {
            ui::text(4, y + 2, T("Short note (what happened), ENTER sends SOS:"), ui::C_DIM);
            ui::text(4, y + 16, note_.value + "_", ui::C_TEXT);
            ui::footer(T("ENTER start SOS  ` cancel"));
            return;
        }
        ui::fill(10, y + 1, 100, 34, ui::C_ERR);
        overCentered(60, y + 7, "SOS", 3);
        ui::text(118, y + 2, T("S: send SOS"), ui::C_TEXT);
        ui::text(118, y + 13, T("N: with a note"), ui::C_DIM);
        ui::text(118, y + 24, T("5 s to cancel"), ui::C_DIM);
        y += 38;
        std::string pos = station::grid();
        ui::text(4, y, std::string(T("Position")) + ": " + (pos.empty() ? T("unknown!") : pos), pos.empty() ? ui::C_ERR : ui::C_TEXT);
        y += 11;
        drawLinks(y);
        y += 13;
        char b[64];
        int left = emergency::timerSecondsLeft();
        if (left >= 0) snprintf(b, sizeof(b), "T %s %d:%02d", T("timer"), left / 60, left % 60);
        else snprintf(b, sizeof(b), "T %s", T("timer off"));
        ui::text(4, y, b, emergency::timerWarning() ? ui::C_ERR : left >= 0 ? ui::C_WARN : ui::C_DIM);
        y += 11;
        ui::text(4, y, std::string("A ") + T("siren"), emergency::siren() ? ui::C_ERR : ui::C_DIM);
        ui::text(70, y, std::string("L ") + T("light"), emergency::strobe() ? ui::C_NOTICE : ui::C_DIM);
        ui::text(136, y, std::string("P ") + T("save"), emergency::saver() ? ui::C_OK : ui::C_DIM);
        ui::footer(emergency::timerWarning() ? T("PRESS ANY KEY or SOS goes out!") : T("TAB: messages, alerts, info"));
    }

    void drawCountdown() {
        ui::fill(0, ui::BODY_Y, ui::W, ui::BODY_H, ui::C_ERR);
        overCentered(ui::W / 2, ui::BODY_Y + 6, T("SOS in"), 2);
        overCentered(ui::W / 2, ui::BODY_Y + 30, std::to_string(emergency::countdownSeconds()), 5);
        overCentered(ui::W / 2, ui::BODY_Y + 82, T("any key cancels"), 1.5);
        ui::footer("");
    }

    void drawActive() {
        ui::fill(0, ui::BODY_Y, ui::W, 16, ui::C_ERR);
        ui::fill(0, ui::BODY_Y + 16, ui::W, ui::BODY_H - 16, ui::C_BG);
        overText(4, ui::BODY_Y + 2, T("SOS ACTIVE"), 1.5);
        char b[64];
        uint32_t next = emergency::nextSendInMs() / 1000;
        snprintf(b, sizeof(b), "#%d  %s %u:%02u", emergency::sentCount(), T("next"), (unsigned)(next / 60), (unsigned)(next % 60));
        overText(130, ui::BODY_Y + 4, b, 1);
        int y = ui::BODY_Y + 19;
        for (const std::string& l : ui::wrapLines(emergency::lastText(), 38)) {
            ui::text(4, y, l, ui::C_NOTICE);
            y += 10;
            if (y > ui::BODY_Y + 40) break;
        }
        ui::text(4, y, std::string(T("via")) + " " + emergency::sentVia(), ui::C_DIM);
        y += 11;
        if (emergency::repeatedByMesh()) {
            ui::text(4, y, T("Repeated by the mesh: it got out"), ui::C_OK);
            y += 11;
        }
        int shown = 0;
        const auto& a = emergency::alerts();
        for (size_t i = a.size(); i-- > 0 && shown < 2;) {
            ui::text(4, y, (a[i].from + ": " + a[i].text).substr(0, 38), ui::C_OK);
            y += 10;
            shown++;
        }
        if (!shown) ui::text(4, y, T("No reply yet. Stay put if you can."), ui::C_DIM);
        drawLinks(ui::H - ui::FOOTER_H - 11);
        ui::footer(confirmStop_ ? T("Stop SOS and send OK? Y") : T("X stop  A siren  L light  TAB replies"));
    }

    void drawMessages() {
        int y = kTop;
        for (int i = 0; i < emergency::presetCount(); i++) {
            ui::text(4, y, std::to_string(i + 1), ui::C_NOTICE);
            ui::text(16, y, emergency::preset(i), i == 2 || i == 4 ? ui::C_ERR : ui::C_TEXT);
            y += 12;
        }
        if (!message_.empty()) ui::text(4, y + 2, message_.substr(0, 38), ui::C_OK);
        ui::footer(T("1-6 send to everyone"));
    }

    void drawAlerts() {
        const auto& a = emergency::alerts();
        if (a.empty()) {
            ui::textCentered(60, T("No distress calls heard"), ui::C_DIM);
        } else {
            std::vector<std::string> rows;
            std::vector<uint16_t> colors;
            for (size_t i = a.size(); i-- > 0;) {
                uint32_t ago = (millis() - a[i].ms) / 60000;
                rows.push_back(std::to_string(ago) + "m " + a[i].from + ": " + a[i].text);
                colors.push_back(a[i].reply ? ui::C_OK : ui::C_ERR);
            }
            ui::list(rows, selected_, scroll_, &colors, kTop, ui::ROWS - 1);
        }
        ui::footer(T("red: calls for help  green: replies"));
    }

    void drawInfo() {
        static const char* lines[] = {
                "IARU R1 emergency: 3.760 7.110 14.300",
                "  18.160 21.360 MHz",
                "Turkey: 112 (phone), AFAD",
                "VHF calling: 145.500 FM simplex",
                "Say: who, where, what happened, need",
                "Stay where you are if you can;",
                "save the battery (P), use the light.",
                "More: Guide > Acil durum"};
        int y = kTop;
        for (const char* l : lines) {
            ui::text(4, y, T(l), ui::C_TEXT);
            y += 11;
        }
        ui::footer(T("TAB pages  ` back"));
    }
};
