#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Net.h"
#include "core/Ui.h"
#include "core/Web.h"
#include "core/Wsjtx.h"

// Second screen for WSJT-X / JTDX: shows what the PC decodes, highlights
// stations calling you and CQs from countries you have not logged, and
// takes logged contacts into the HamOS logbook.
class Ft8App : public App {
public:
    const char* name() const override { return "FT8"; }
    uint16_t color() const override { return 0xF81F; }

    void onEnter() override { follow_ = true; }

    void onTick(uint32_t now) override {
        if (now - lastMs_ >= 500) {     // decodes arrive in the background
            lastMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.ch == 'c' || k.ch == 'C') {
            cqOnly_ = !cqOnly_;
            follow_ = true;
        } else if (k.up() || k.down()) {
            int count = (int)visible().size();
            if (count == 0) return;
            selected_ += k.down() ? 1 : -1;
            if (selected_ < 0) selected_ = 0;
            if (selected_ >= count - 1) { selected_ = count - 1; follow_ = true; }    // at the newest: keep following
            else follow_ = false;
        } else {
            return;
        }
        dirty = true;
    }

    void draw() override {
        if (!wsjtxlink::enabled() || !wsjtxlink::listening() || (wsjtxlink::decodes().empty() && !wsjtxlink::active())) {
            ui::clearBody();
            if (!wsjtxlink::enabled()) {
                ui::textCentered(ui::BODY_Y + 30, "Switched off", ui::C_WARN, 1.5);
                ui::textCentered(ui::BODY_Y + 54, "Set WSJT-X link to 1 in Settings", ui::C_DIM);
            } else if (!wsjtxlink::listening()) {
                ui::textCentered(ui::BODY_Y + 40, "Waiting for WiFi", ui::C_WARN, 1.5);
            } else {
                ui::textCentered(ui::BODY_Y + 14, "Waiting for WSJT-X", ui::C_DIM, 1.5);
                ui::textCentered(ui::BODY_Y + 40, "In WSJT-X: Settings > Reporting", ui::C_DIM);
                ui::textCentered(ui::BODY_Y + 54, "UDP Server = " + net::address() + "  port 2237", ui::C_TEXT);
            }
            ui::footer("` back");
            return;
        }
        std::vector<const wsjtxlink::Decode*> rows = visible();
        std::vector<std::string> text;
        std::vector<uint16_t> colors;
        char line[64];
        for (const wsjtxlink::Decode* d : rows) {
            snprintf(line, sizeof(line), "%+3d %4u %-22.22s %.6s", d->snr, (unsigned)d->df, d->text.c_str(),
                     d->entity ? d->entity->name : "");
            text.push_back(line);
            colors.push_back(d->toMe ? ui::C_OK : (d->cq && d->newEntity) ? ui::C_WARN : d->cq ? ui::C_TEXT : ui::C_DIM);
        }
        if (follow_ || selected_ >= (int)rows.size()) selected_ = rows.empty() ? 0 : (int)rows.size() - 1;
        if (rows.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 40, "No CQ calls yet", ui::C_DIM);
        } else {
            ui::list(text, selected_, scroll_, &colors);
        }
        snprintf(line, sizeof(line), "%s%s%s  C %s  log +%u", wsjtxlink::dial().c_str(), wsjtxlink::transmitting() ? " TX" : "",
                 wsjtxlink::active() ? "" : " (idle)", cqOnly_ ? "all" : "CQ only", (unsigned)wsjtxlink::logged());
        ui::footer(line);
    }

private:
    std::vector<const wsjtxlink::Decode*> visible() const {
        std::vector<const wsjtxlink::Decode*> out;
        for (const wsjtxlink::Decode& d : wsjtxlink::decodes())
            if (!cqOnly_ || d.cq || d.toMe) out.push_back(&d);
        return out;
    }

    bool cqOnly_ = false, follow_ = true;
    int selected_ = 0, scroll_ = 0;
    uint32_t lastMs_ = 0;
};
