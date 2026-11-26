#pragma once
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/AprsIs.h"
#include "core/Flags.h"
#include "lib/Dxcc.h"
#include "core/Audio.h"
#include "core/LoraAprs.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Ax25.h"
#include "lib/Maidenhead.h"

// APRS: nearby stations and messages over the internet (APRS-IS), and a
// 1200 baud packet modem for a real radio: it decodes what the microphone
// hears and plays your beacon through the speaker. TAB cycles the pages.
class AprsApp : public App {
public:
    const char* name() const override { return "APRS"; }
    uint16_t color() const override { return 0x051F; }

    void onEnter() override {
        input_.maxLen = 76;
        status_.clear();
        if (page_ == AUDIO) startAudio();
    }

    void onExit() override {
        audio::stopCapture();
        detail_ = false;
    }

    bool onBack() override {
        if (page_ == MESSAGES && detail_) {         // back to the message list
            detail_ = false;
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) {
            if (page_ == AUDIO) audio::stopCapture();
            page_ = (page_ + 1) % 3;
            selected_ = scroll_ = 0;
            status_.clear();
            if (page_ == AUDIO) startAudio();
        } else if (page_ == STATIONS) {
            if (k.ch == 'b' || k.ch == 'B') {
                std::string error;
                status_ = aprsis::sendBeacon(error) ? (loraaprs::ready() ? T("Position sent on LoRa") : T("Position sent to APRS-IS")) : error;
            } else if (!ui::listNav(k, selected_, (int)aprsis::stations().size())) {
                return;
            }
        } else if (page_ == MESSAGES) {
            int count = (int)aprsis::messages().size();
            if (detail_) {                          // one message in full
                if (k.ch == 'r' || k.ch == 'R') {
                    const aprsis::Message& m = aprsis::messages()[(size_t)msgSel_];
                    input_.value = (m.mine ? m.to : m.from) + " ";
                    detail_ = false;
                } else if (k.fnUp() || k.up()) {
                    if (msgSel_ > 0) msgSel_--;
                } else if (k.fnDown() || k.down()) {
                    if (msgSel_ < count - 1) msgSel_++;
                } else if (k.enter) {
                    detail_ = false;
                } else {
                    return;
                }
                dirty = true;
                return;
            }
            // Arrows pick a message; without text typed the plain ; and . keys work too.
            bool empty = input_.value.empty();
            if (count > 0 && (k.fnUp() || (empty && k.up()))) {
                msgSel_ = msgSel_ < 0 || msgSel_ >= count ? count - 1 : std::max(0, msgSel_ - 1);
            } else if (count > 0 && (k.fnDown() || (empty && k.down()))) {
                msgSel_ = msgSel_ < 0 ? count - 1 : std::min(count - 1, msgSel_ + 1);
            } else if (k.enter && empty) {
                if (count == 0) return;
                if (msgSel_ < 0 || msgSel_ >= count) msgSel_ = count - 1;
                detail_ = true;
            } else if (k.enter) {
                size_t space = input_.value.find(' ');
                std::string error;
                std::string to = input_.value.substr(0, space);
                std::string text = space == std::string::npos ? "" : input_.value.substr(space + 1);
                if (aprsis::sendMessage(to, text, error)) {
                    input_.value = to + " ";            // ready for the next line to the same station
                    status_.clear();
                } else {
                    status_ = error;
                }
            } else if (!input_.handle(k)) {
                return;
            }
        } else if ((k.ch == 'b' || k.ch == 'B') && !sending_) {
            std::string packet = aprsis::beaconPacket("WIDE1-1,WIDE2-1"), frame;
            if (packet.empty() || !ax25::encodeFrame(packet, frame)) {
                status_ = T("Needs a callsign and a position (grid or GPS)");
            } else {
                audio::play(ax25::modulate(frame), ax25::SAMPLE_RATE);
        audio::markTransmit();      // stops the microphone
                sending_ = true;
                status_ = T("Sending beacon through the speaker...");
            }
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (sending_) {
            if (audio::playing()) return;
            sending_ = false;
            status_.clear();
            startAudio();
            dirty = true;
        } else if (page_ == AUDIO) {
            const int16_t* block = audio::capture();
            if (block) {
                modem_.feed(block, audio::BLOCK);
                for (const std::string& packet : modem_.packets) {
                    aprsis::ingest(packet, aprsis::AUDIO);
                    heard_.push_back(packet);
                    if (heard_.size() > 12) heard_.erase(heard_.begin());
                    dirty = true;
                }
                modem_.packets.clear();
            }
        }
        if (now - lastMs_ >= 1000) {        // data arrives in the background
            lastMs_ = now;
            dirty = true;
        }
    }

    void draw() override {
        if (page_ == STATIONS) drawStations();
        else if (page_ == MESSAGES && detail_) drawMessage();
        else if (page_ == MESSAGES) drawMessages();
        else drawAudio();
    }

private:
    enum { STATIONS, MESSAGES, AUDIO };

    void startAudio() {
        modem_ = ax25::Demodulator();
        micOk_ = audio::startCapture(ax25::SAMPLE_RATE);
    }

    void drawStations() {
        std::vector<aprsis::Station> list = aprsis::stations();
        double lat = 0, lon = 0;
        bool here = station::position(lat, lon);
        std::sort(list.begin(), list.end(), [&](const aprsis::Station& a, const aprsis::Station& b) {
            if (here) return maidenhead::distanceKm(lat, lon, a.lat, a.lon) < maidenhead::distanceKm(lat, lon, b.lat, b.lon);
            return millis() - a.lastHeardMs < millis() - b.lastHeardMs;
        });
        if (list.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 30, T("No stations yet"), ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 56, aprsis::status(), aprsis::online() ? ui::C_DIM : ui::C_WARN);
            ui::footer(T("TAB messages   B send position   ` back"));
            return;
        }
        std::vector<std::string> rows;
        std::vector<uint16_t> colors;
        char line[64];
        for (const aprsis::Station& s : list) {
            uint32_t age = (millis() - s.lastHeardMs) / 60000;
            char where[24] = "";
            if (here) snprintf(where, sizeof(where), "%5.1fkm %03.0f", maidenhead::distanceKm(lat, lon, s.lat, s.lon),
                               maidenhead::bearingDeg(lat, lon, s.lat, s.lon));
            static const char* via[3] = {"", "RF", "LoRa"};
            snprintf(line, sizeof(line), "%-9.9s %-12s %3um %s", s.call.c_str(), where, (unsigned)age, via[s.source]);
            rows.push_back(line);
            colors.push_back(s.viaRadio() ? ui::C_OK : ui::C_TEXT);
        }
        if (selected_ >= (int)list.size()) selected_ = (int)list.size() - 1;
        ui::list(rows, selected_, scroll_, &colors);
        for (int i = 0; i < ui::ROWS && scroll_ + i < (int)list.size(); i++)
            if (const dxcc::Entity* e = dxcc::lookup(list[(size_t)(scroll_ + i)].call.substr(0, list[(size_t)(scroll_ + i)].call.find('-'))))
                flags::drawSmall(e->iso, 6 + 34 * 6, ui::BODY_Y + i * ui::ROW_H + 1);
        std::string hint = !status_.empty() ? status_ : ui::displayable(list[(size_t)selected_].comment);
        if (hint.empty()) hint = maidenhead::fromLatLon(list[(size_t)selected_].lat, list[(size_t)selected_].lon);
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.c_str());
    }

    // One line per message, newest at the bottom; the selected one is highlighted.
    void drawMessages() {
        aprsis::markRead();
        ui::clearBody();
        const auto& all = aprsis::messages();
        const int rows = ui::ROWS - 1;
        int count = (int)all.size();
        if (count != seenCount_) {                  // a new message: follow it unless one is picked
            if (msgSel_ < 0 || msgSel_ >= seenCount_ - 1) msgSel_ = count - 1;
            seenCount_ = count;
        }
        if (msgSel_ >= count) msgSel_ = count - 1;
        if (all.empty()) {
            ui::textCentered(ui::BODY_Y + 28, T("No messages"), ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 52, T("Type: CALLSIGN your text, then ENTER"), ui::C_DIM);
        }
        int first = std::max(0, count - rows);      // the newest fill the screen ...
        if (msgSel_ >= 0 && msgSel_ < first) first = msgSel_;   // ... unless an older one is picked
        for (int i = first; i < count && i < first + rows; i++) {
            const aprsis::Message& m = all[(size_t)i];
            // Sent messages turn green once the other station has acknowledged them.
            uint16_t color = m.mine ? (m.acked ? ui::C_OK : ui::C_TEXT) : (uint16_t)0xAEDC;
            std::string line = ui::displayable(m.mine ? "> " + m.to + ": " + m.text : m.from + ": " + m.text);
            std::vector<std::string> cut = ui::wrapLines(line, 38);
            std::string shown = cut.empty() ? "" : cut[0] + (cut.size() > 1 ? "~" : "");
            int y = ui::BODY_Y + 2 + (i - first) * ui::ROW_H;
            if (i == msgSel_) {
                ui::fill(0, y - 2, ui::W, ui::ROW_H, ui::C_SEL);
                ui::textOn(4, y, shown, ui::C_TEXT, ui::C_SEL);
            } else {
                ui::text(4, y, shown, color);
            }
        }
        int y = ui::BODY_Y + rows * ui::ROW_H;
        ui::fill(0, y, ui::W, ui::ROW_H, ui::C_FOOT);
        std::string shown = input_.value.size() > 36 ? input_.value.substr(input_.value.size() - 36) : input_.value;
        ui::textOn(4, y + 2, "> " + shown + "_", ui::C_TEXT, ui::C_FOOT);
        std::string hint = !status_.empty()          ? status_
                           : input_.value.empty() && count > 0 ? T("Arrows pick  ENTER open  type to write")
                           : loraaprs::ready()        ? T("ENTER send on LoRa   TAB radio modem")
                           : aprsis::online()         ? T("ENTER send   TAB radio modem")
                                                      : aprsis::status();
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.c_str());
    }

    // The selected message in full: who, when, which way, and the whole text.
    void drawMessage() {
        ui::clearBody();
        const auto& all = aprsis::messages();
        if (msgSel_ < 0 || msgSel_ >= (int)all.size()) {
            detail_ = false;
            drawMessages();
            return;
        }
        const aprsis::Message& m = all[(size_t)msgSel_];
        int y = ui::BODY_Y + 2;
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        std::string head = m.mine ? std::string(T("To")) + " " + m.to : std::string(T("From")) + " " + m.from;
        ui::textOn(4, y, head, ui::C_NOTICE, ui::C_FOOT);
        if (const dxcc::Entity* e = dxcc::lookup((m.mine ? m.to : m.from).substr(0, (m.mine ? m.to : m.from).find('-'))))
            flags::drawSmall(e->iso, ui::W - 22, ui::BODY_Y + 1);
        y += 13;
        char when[48];
        uint32_t ago = (millis() - m.atMs) / 60000;
        if (m.utcMs > 0) {
            time_t t = (time_t)(m.utcMs / 1000);
            struct tm tm;
            gmtime_r(&t, &tm);
            snprintf(when, sizeof(when), "%02d:%02dz  (%u %s)", tm.tm_hour, tm.tm_min, (unsigned)ago, T("min ago"));
        } else {
            snprintf(when, sizeof(when), "%u %s", (unsigned)ago, T("min ago"));
        }
        static const char* via[3] = {"APRS-IS", "RF", "LoRa"};
        std::string info = std::string(when) + "  " + via[m.source];
        ui::text(4, y, info, ui::C_DIM);
        y += 11;
        std::string state = m.mine ? (m.acked ? T("Acknowledged") : T("Not acknowledged yet"))
                                   : std::string(T("To")) + " " + m.to;
        if (!m.id.empty()) state += "  #" + m.id;
        ui::text(4, y, state, m.mine ? (m.acked ? ui::C_OK : ui::C_WARN) : ui::C_DIM);
        y += 11;
        if (!m.path.empty()) {
            std::string path = ui::ascii(m.path);
            if (path.size() > 38) path = path.substr(0, 37) + "~";
            ui::text(4, y, path, ui::C_DIM);
            y += 11;
        }
        ui::line(4, y, ui::W - 4, y, ui::C_FOOT);
        y += 3;
        for (const std::string& l : ui::wrapLines(ui::displayable(m.text), 25)) {
            if (y > ui::H - ui::FOOTER_H - 12) break;
            ui::text(4, y, l, ui::C_TEXT, 1.5);
            y += 13;
        }
        char hint[48];
        snprintf(hint, sizeof(hint), "%d/%d  %s", msgSel_ + 1, (int)all.size(), T("R reply  arrows next  ` back"));
        ui::footer(hint);
    }

    void drawAudio() {
        ui::clearBody();
        char line[64];
        snprintf(line, sizeof(line), "1200 baud modem   %u ok  %u bad", (unsigned)modem_.frames, (unsigned)modem_.crcErrors);
        ui::text(6, ui::BODY_Y + 3, line, ui::C_SEL);
        if (!micOk_ && !sending_) {
            ui::textCentered(ui::BODY_Y + 50, T("Microphone not available"), ui::C_WARN);
        } else if (heard_.empty()) {
            ui::textCentered(ui::BODY_Y + 38, T("Listening on the microphone..."), ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 54, T("Tune a radio to 144.800 MHz and hold"), ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 66, T("the Cardputer next to its speaker"), ui::C_DIM);
        } else {
            // Newest packets at the bottom, two lines each.
            const size_t width = 38;
            std::vector<std::string> lines;
            for (const std::string& p : heard_) {
                std::string a = ui::ascii(p);
                lines.push_back(a.substr(0, width));
                if (a.size() > width) lines.push_back("  " + a.substr(width, width - 2));
            }
            size_t rows = 7, first = lines.size() > rows ? lines.size() - rows : 0;
            for (size_t i = first; i < lines.size(); i++)
                ui::text(6, ui::BODY_Y + 18 + (int)(i - first) * ui::ROW_H, lines[i], lines[i][0] == ' ' ? ui::C_DIM : ui::C_TEXT);
        }
        ui::footer(!status_.empty() ? status_.c_str() : T("B send beacon by speaker   TAB stations"));
    }

    int page_ = STATIONS, selected_ = 0, scroll_ = 0;
    int msgSel_ = -1, seenCount_ = 0;       // selected message (-1: the newest)
    bool detail_ = false;
    uint32_t lastMs_ = 0;
    ui::TextInput input_;
    std::string status_;
    ax25::Demodulator modem_;
    std::vector<std::string> heard_;
    bool micOk_ = false, sending_ = false;
};
