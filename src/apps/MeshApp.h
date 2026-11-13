#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Cap.h"
#include "core/Config.h"
#include "core/Mesh.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Maidenhead.h"

// Meshtastic chat, node list and radio status. TAB cycles the three pages.
class MeshApp : public App {
public:
    const char* name() const override { return "Mesh"; }
    uint16_t color() const override { return 0x67E0; }

    void onEnter() override {
        input_.maxLen = 200;
        scrollBack_ = 0;
        status_.clear();
    }

    void onTick(uint32_t now) override {
        if (now - lastMs_ >= 1000) {        // messages arrive in the background
            lastMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) {
            page_ = (page_ + 1) % 3;
            selected_ = scroll_ = 0;
        } else if (page_ == CHAT) {
            if (k.fnUp()) scrollBack_++;
            else if (k.fnDown()) scrollBack_ = scrollBack_ > 0 ? scrollBack_ - 1 : 0;
            else if (k.enter) {
                if (input_.value.empty()) return;
                std::string error;
                if (mesh::sendText(input_.value, error)) {
                    input_.value.clear();
                    status_.clear();
                    scrollBack_ = 0;
                } else {
                    status_ = error;
                }
            } else if (!input_.handle(k)) {
                return;
            }
        } else if (page_ == NODES) {
            if (!ui::listNav(k, selected_, (int)mesh::nodes().size())) return;
        } else {
            return;
        }
        dirty = true;
    }

    void draw() override {
        if (!mesh::ready()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 24, "Mesh not running", ui::C_WARN, 2);
            ui::textCentered(ui::BODY_Y + 56, mesh::status(), ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 72, "Needs the LoRa cap. See Settings: LoRa mode.", ui::C_DIM);
            ui::footer("` back");
            return;
        }
        if (page_ == CHAT) drawChat();
        else if (page_ == NODES) drawNodes();
        else drawInfo();
    }

    const std::string& inputForTest() const { return input_.value; }    // simulator checks

private:
    enum { CHAT, NODES, INFO };
    struct Line {
        std::string text;
        uint16_t color;
    };

    static void wrap(const std::string& text, uint16_t color, std::vector<Line>& out) {
        bool first = true;
        for (const std::string& part : ui::wrapLines(text, 38)) {
            out.push_back({(first ? "" : "  ") + part, color});
            first = false;
        }
    }

    void drawChat() {
        mesh::markRead();
        std::vector<Line> lines;
        for (const mesh::Message& m : mesh::messages()) {
            std::string text = ui::displayable(m.text);
            if (m.mine) wrap("> " + text, m.relayed ? ui::C_OK : m.sent ? ui::C_TEXT : ui::C_DIM, lines);
            else wrap((m.direct ? "[DM] " : "") + ui::displayable(mesh::nodeName(m.from, true)) + ": " + text,
                      m.direct ? ui::C_WARN : 0xAEDC, lines);
        }
        const int rows = ui::ROWS - 1;                  // last row is the input line
        int maxBack = std::max(0, (int)lines.size() - rows);
        if (scrollBack_ > maxBack) scrollBack_ = maxBack;
        ui::clearBody();
        if (lines.empty()) ui::textCentered(ui::BODY_Y + 36, "No messages yet", ui::C_DIM);
        int first = std::max(0, (int)lines.size() - rows - scrollBack_);
        for (int i = 0; i < rows && first + i < (int)lines.size(); i++)
            ui::text(4, ui::BODY_Y + 2 + i * ui::ROW_H, lines[first + i].text, lines[first + i].color);

        int y = ui::BODY_Y + rows * ui::ROW_H;
        ui::fill(0, y, ui::W, ui::ROW_H, ui::C_FOOT);
        std::string shown = input_.value.size() > 36 ? input_.value.substr(input_.value.size() - 36) : input_.value;
        ui::textOn(4, y + 2, "> " + shown + "_", ui::C_TEXT, ui::C_FOOT);

        char hint[64];
        if (!status_.empty()) snprintf(hint, sizeof(hint), "%s", status_.c_str());
        else snprintf(hint, sizeof(hint), "ENTER send  TAB nodes(%d)  Fn+arrows", (int)mesh::nodes().size());
        ui::footer(hint);
    }

    void drawNodes() {
        std::vector<mesh::Node> nodes = mesh::nodes();
        std::sort(nodes.begin(), nodes.end(), [](const mesh::Node& a, const mesh::Node& b) {
            return millis() - a.lastHeardMs < millis() - b.lastHeardMs;     // most recent first
        });
        if (nodes.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 36, "No nodes heard yet", ui::C_DIM);
            ui::footer("TAB info   ` back");
            return;
        }
        std::vector<std::string> rows;
        double lat, lon;
        bool here = station::position(lat, lon);
        char line[64];
        for (const mesh::Node& n : nodes) {
            uint32_t age = (millis() - n.lastHeardMs) / 1000;
            char ageText[8];
            if (age < 60) snprintf(ageText, sizeof(ageText), "%us", (unsigned)age);
            else if (age < 3600) snprintf(ageText, sizeof(ageText), "%um", (unsigned)(age / 60));
            else snprintf(ageText, sizeof(ageText), "%uh", (unsigned)(age / 3600));
            char where[20] = "";
            if (here && n.hasPosition)
                snprintf(where, sizeof(where), "%.0fkm %03.0f", maidenhead::distanceKm(lat, lon, n.lat, n.lon),
                         maidenhead::bearingDeg(lat, lon, n.lat, n.lon));
            snprintf(line, sizeof(line), "%-4.4s %-12.12s %+5.1f %-3s %s", ui::ascii(mesh::nodeName(n.id, true)).c_str(),
                     ui::ascii(mesh::nodeName(n.id)).c_str(), n.snr, ageText, where);
            rows.push_back(line);
        }
        ui::list(rows, selected_, scroll_);
        const mesh::Node& sel = nodes[(size_t)selected_];
        snprintf(line, sizeof(line), "!%08x  %.0f dBm  %d hop%s  TAB info", (unsigned)sel.id, sel.rssi, sel.hops,
                 sel.hops == 1 ? "" : "s");
        ui::footer(line);
    }

    void drawInfo() {
        mesh::Stats s = mesh::stats();
        ui::clearBody();
        char line[64];
        int y = ui::BODY_Y + 3;
        auto row = [&](const char* label, const std::string& value, uint16_t color = ui::C_TEXT) {
            ui::text(6, y, label, ui::C_DIM);
            ui::text(78, y, value, color);
            y += ui::ROW_H;
        };
        row("This node", ui::ascii(mesh::nodeName(mesh::myId())) + "  (" + ui::ascii(mesh::nodeName(mesh::myId(), true)) + ")");
        snprintf(line, sizeof(line), "!%08x", (unsigned)mesh::myId());
        row("Node id", line);
        snprintf(line, sizeof(line), "%s  %.3f MHz", s.channel.c_str(), s.freqMhz);
        row("Channel", line, ui::C_OK);
        row("Region", s.region + "  " + s.preset);
        snprintf(line, sizeof(line), "%u in  %u out  %u queued", (unsigned)s.received, (unsigned)s.sent, (unsigned)s.queued);
        row("Packets", line);
        snprintf(line, sizeof(line), "%u other ch, %u unreadable", (unsigned)s.otherChannel, (unsigned)s.undecodable);
        row("Ignored", line);
        snprintf(line, sizeof(line), "%d%% of the hourly limit", s.airtimePercentOfLimit);
        row("Airtime", line, s.airtimePercentOfLimit >= 90 ? ui::C_ERR : ui::C_TEXT);
        row("Position", config::get("mesh_pos", "0") == "1" ? "shared, coarse (~3 km)" : "not shared");
        row("Cap", cap::hasExpander() ? "LoRa-1262 (expander found)" : "no expander (LoRa868?)");
        ui::footer("TAB chat   ` back");
    }

    int page_ = CHAT, selected_ = 0, scroll_ = 0, scrollBack_ = 0;
    uint32_t lastMs_ = 0;
    ui::TextInput input_;
    std::string status_;
};
