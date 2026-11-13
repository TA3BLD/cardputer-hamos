#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Audio.h"
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

    void onExit() override { audio::stopCapture(); }

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
                status_ = aprsis::sendBeacon(error) ? "Position sent to APRS-IS" : error;
            } else if (!ui::listNav(k, selected_, (int)aprsis::stations().size())) {
                return;
            }
        } else if (page_ == MESSAGES) {
            if (k.enter) {
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
                status_ = "Needs a callsign and a position (grid or GPS)";
            } else {
                audio::play(ax25::modulate(frame), ax25::SAMPLE_RATE);      // stops the microphone
                sending_ = true;
                status_ = "Sending beacon through the speaker...";
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
                    aprsis::ingest(packet, true);
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
            ui::textCentered(ui::BODY_Y + 30, "No stations yet", ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 56, aprsis::status(), aprsis::online() ? ui::C_DIM : ui::C_WARN);
            ui::footer("TAB messages   B send position   ` back");
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
            snprintf(line, sizeof(line), "%-9.9s %-12s %3um %s", s.call.c_str(), where, (unsigned)age, s.viaRadio ? "RF" : "");
            rows.push_back(line);
            colors.push_back(s.viaRadio ? ui::C_OK : ui::C_TEXT);
        }
        if (selected_ >= (int)list.size()) selected_ = (int)list.size() - 1;
        ui::list(rows, selected_, scroll_, &colors);
        std::string hint = !status_.empty() ? status_ : ui::ascii(list[(size_t)selected_].comment);
        if (hint.empty()) hint = maidenhead::fromLatLon(list[(size_t)selected_].lat, list[(size_t)selected_].lon);
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.c_str());
    }

    void drawMessages() {
        aprsis::markRead();
        ui::clearBody();
        const auto& all = aprsis::messages();
        const int rows = ui::ROWS - 1;
        if (all.empty()) {
            ui::textCentered(ui::BODY_Y + 28, "No messages", ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 52, "Type: CALLSIGN your text, then ENTER", ui::C_DIM);
        }
        // Sent messages turn green once the other station has acknowledged them.
        std::vector<std::string> lines;
        std::vector<uint16_t> colors;
        for (const aprsis::Message& m : all) {
            std::string text = ui::ascii(m.mine ? "> " + m.to + ": " + m.text : m.from + ": " + m.text);
            for (const std::string& part : ui::wrapLines(text, 38)) {
                lines.push_back(part);
                colors.push_back(m.mine ? (m.acked ? ui::C_OK : ui::C_TEXT) : 0xAEDC);
            }
        }
        int first = std::max(0, (int)lines.size() - rows);
        for (int i = first; i < (int)lines.size(); i++)
            ui::text(4, ui::BODY_Y + 2 + (i - first) * ui::ROW_H, lines[(size_t)i], colors[(size_t)i]);
        int y = ui::BODY_Y + rows * ui::ROW_H;
        ui::fill(0, y, ui::W, ui::ROW_H, ui::C_FOOT);
        std::string shown = input_.value.size() > 36 ? input_.value.substr(input_.value.size() - 36) : input_.value;
        ui::textOn(4, y + 2, "> " + shown + "_", ui::C_TEXT, ui::C_FOOT);
        std::string hint = !status_.empty() ? status_ : aprsis::online() ? "ENTER send   TAB radio modem" : aprsis::status();
        if (hint.size() > 39) hint.resize(39);
        ui::footer(hint.c_str());
    }

    void drawAudio() {
        ui::clearBody();
        char line[64];
        snprintf(line, sizeof(line), "1200 baud modem   %u ok  %u bad", (unsigned)modem_.frames, (unsigned)modem_.crcErrors);
        ui::text(6, ui::BODY_Y + 3, line, ui::C_SEL);
        if (!micOk_ && !sending_) {
            ui::textCentered(ui::BODY_Y + 50, "Microphone not available", ui::C_WARN);
        } else if (heard_.empty()) {
            ui::textCentered(ui::BODY_Y + 38, "Listening on the microphone...", ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 54, "Tune a radio to 144.800 MHz and hold", ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 66, "the Cardputer next to its speaker", ui::C_DIM);
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
        ui::footer(!status_.empty() ? status_.c_str() : "B send beacon by speaker   TAB stations");
    }

    int page_ = STATIONS, selected_ = 0, scroll_ = 0;
    uint32_t lastMs_ = 0;
    ui::TextInput input_;
    std::string status_;
    ax25::Demodulator modem_;
    std::vector<std::string> heard_;
    bool micOk_ = false, sending_ = false;
};
