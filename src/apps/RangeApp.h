#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Mesh.h"
#include "core/RangeTest.h"
#include "core/Ui.h"

// LoRa range test with a second Meshtastic device: S sends numbered packets,
// R listens and lists what arrives (signal, distance). The receptions are
// drawn on the Map as coloured dots and saved to /HamOS/rangetest.csv.
class RangeApp : public App {
public:
    const char* name() const override { return "Range test"; }
    uint16_t color() const override { return 0x07FF; }

    void onTick(uint32_t now) override {
        size_t n = rangetest::hits().size() + rangetest::sentCount();
        if (n != shown_ || now - lastMs_ > 1000) {
            shown_ = n;
            lastMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.ch == 's' || k.ch == 'S') rangetest::setRole(rangetest::role() == rangetest::Role::Send ? rangetest::Role::Off : rangetest::Role::Send);
        else if (k.ch == 'r' || k.ch == 'R') rangetest::setRole(rangetest::role() == rangetest::Role::Receive ? rangetest::Role::Off : rangetest::Role::Receive);
        else if (ui::listNav(k, selected_, (int)rangetest::hits().size())) {}
        else return;
        dirty = true;
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        rangetest::Role r = rangetest::role();
        char head[80];
        if (r == rangetest::Role::Send)
            snprintf(head, sizeof(head), "%s: %u  (%s %d s)", T("Sending"), (unsigned)rangetest::sentCount(), T("every"), rangetest::intervalSeconds());
        else if (r == rangetest::Role::Receive)
            snprintf(head, sizeof(head), "%s: %u  %s %d  %s %.1f km", T("Heard"), (unsigned)rangetest::hits().size(), T("lost"),
                     rangetest::lost(), T("best"), rangetest::bestKm());
        else
            snprintf(head, sizeof(head), "%s", T("Range test off"));
        ui::text(4, ui::BODY_Y + 2, head, r == rangetest::Role::Off ? ui::C_DIM : ui::C_NOTICE);
        int top = ui::BODY_Y + 13;
        if (!mesh::ready()) {
            ui::textCentered(60, mesh::status(), ui::C_WARN);
        } else if (r == rangetest::Role::Receive && !rangetest::hits().empty()) {
            std::vector<std::string> rows;
            std::vector<uint16_t> colors;
            const auto& h = rangetest::hits();
            for (size_t i = h.size(); i-- > 0;) {
                char line[64];
                char dist[16] = "     -";
                if (h[i].km >= 0) snprintf(dist, sizeof(dist), "%5.2fkm", h[i].km);
                snprintf(line, sizeof(line), "#%-4u %-6.6s %+5.1fdB %4.0f %s", (unsigned)h[i].seq,
                         mesh::nodeName(h[i].from, true).c_str(), h[i].snr, h[i].rssi, dist);
                rows.push_back(line);
                colors.push_back(snrColor(h[i].snr));
            }
            ui::list(rows, selected_, scroll_, &colors, top, ui::ROWS - 1);
        } else {
            int y = top + 8;
            const char* help = r == rangetest::Role::Send ? T("Take the other device and walk away; it lists what it hears.")
                             : r == rangetest::Role::Receive ? T("Waiting for the sender... Its position must be known for distances.")
                             : T("S: this device sends.  R: this device listens.  The other device does the opposite (HamOS or Meshtastic Range Test).");
            for (const std::string& l : ui::wrapLines(help, 38)) { ui::text(4, y, l, ui::C_DIM); y += 11; }
        }
        ui::footer(T("S send  R receive  (again: stop)"));
    }

    static uint16_t snrColor(float snr) { return snr > 0 ? ui::C_OK : snr > -10 ? ui::C_NOTICE : snr > -15 ? ui::C_WARN : ui::C_ERR; }

private:
    int selected_ = 0, scroll_ = 0;
    size_t shown_ = 0;
    uint32_t lastMs_ = 0;
};
