#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Net.h"
#include "core/Ui.h"
#include "core/Updater.h"
#include "core/Version.h"

// Firmware update from GitHub: shows what is new and installs only after Y.
class UpdateApp : public App {
public:
    const char* name() const override { return T("Update"); }
    uint16_t color() const override { return 0x3666; }

    void onEnter() override {
        scroll_ = 0;
        confirm_ = false;
    }

    void onKey(const KeyEvent& k) override {
        using updater::State;
        const updater::Status& s = updater::status();
        if (s.state == State::Installing || s.state == State::Installed) return;
        if (s.state == State::Available) {
            if (k.ch == 'y' || k.ch == 'Y') {
                if (!confirm_) {
                    confirm_ = true;            // second Y: really install
                } else {
                    self = this;
                    updater::onProgress([] { self->drawProgress(); });
                    draw();
                    bool ok = updater::install();
                    updater::onProgress(nullptr);
                    confirm_ = false;
                    draw();
                    if (ok) updater::restartNow();
                }
                dirty = true;
                return;
            }
            if (k.ch == 'n' || k.ch == 'N') {
                confirm_ = false;
                dirty = true;
                return;
            }
            if (k.down()) { scroll_++; dirty = true; return; }
            if (k.up() && scroll_ > 0) { scroll_--; dirty = true; return; }
        }
        if (k.enter) {
            ui::fill(0, ui::BODY_Y + 40, ui::W, 14, ui::C_BG);
            ui::textCentered(ui::BODY_Y + 42, T("Checking for updates..."), ui::C_DIM);
            updater::check();
            scroll_ = 0;
            confirm_ = false;
            dirty = true;
        }
    }

    void draw() override {
        using updater::State;
        const updater::Status& s = updater::status();
        ui::clearBody();
        int y = ui::BODY_Y + 2;
        ui::text(4, y, std::string(T("Installed version")) + ": " + HAMOS_VERSION, ui::C_DIM);
        y += 12;
        std::string undone = updater::rolledBackFrom();
        if (!undone.empty()) {
            ui::text(4, y, T("The new version did not start; the old one is back"), ui::C_WARN);
            y += 12;
        }
        if (s.state == State::Available || s.state == State::Installing || s.state == State::Installed) {
            char head[64];
            snprintf(head, sizeof(head), "%s: %s (%.1f MB)", T("Update available"), s.latest.version.c_str(),
                     s.latest.size / 1048576.0);
            ui::text(4, y, head, ui::C_OK);
            y += 13;
        }
        if (s.state == State::Installing || s.state == State::Installed) {
            ui::text(4, y + 4, s.message, s.state == State::Installed ? ui::C_OK : ui::C_WARN);
            drawProgress();
            ui::footer("");
            return;
        }
        if (s.state == State::Available) {
            if (confirm_) {
                ui::text(4, y, T("Install it? Y yes  N no"), ui::C_NOTICE, 1.5);
                y += 20;
                for (const std::string& l : ui::wrapLines(T("Settings, log and pictures stay on the SD card."), 38)) {
                    ui::text(4, y, l, ui::C_DIM);
                    y += 11;
                }
                for (const std::string& l : ui::wrapLines(T("If the new version does not start, the old one comes back by itself."), 38)) {
                    ui::text(4, y, l, ui::C_DIM);
                    y += 11;
                }
                ui::footer(T("Y install   N later"));
                return;
            }
            std::vector<std::string> lines = ui::wrapLines(notes(s.latest.notes), 38);
            int rows = (ui::H - ui::FOOTER_H - y) / 11;
            if (scroll_ > (int)lines.size() - rows) scroll_ = std::max(0, (int)lines.size() - rows);
            for (int i = 0; i < rows && scroll_ + i < (int)lines.size(); i++) ui::text(4, y + i * 11, lines[scroll_ + i]);
            ui::footer(T("Y install   N later"));
            return;
        }
        if (s.state != State::Idle) {
            uint16_t c = s.state == State::Failed ? ui::C_ERR : s.state == State::UpToDate ? ui::C_OK : ui::C_DIM;
            int yy = y + 14;
            for (const std::string& l : ui::wrapLines(s.message, 38)) {
                ui::text(4, yy, l, c);
                yy += 11;
            }
        } else if (!net::connected()) {
            ui::text(4, y + 14, T("No WiFi"), ui::C_WARN);
        }
        ui::footer(T("ENTER check   ` back"));
    }

private:
    int scroll_ = 0;
    bool confirm_ = false;
    static inline UpdateApp* self = nullptr;

    void drawProgress() {
        const updater::Status& s = updater::status();
        int x = 10, y = 80, w = ui::W - 20;
        ui::frame(x, y, w, 12, ui::C_TEXT);
        ui::fill(x + 2, y + 2, (w - 4) * s.percent / 100, 8, ui::C_OK);
        ui::fill(x, y + 16, w, 10, ui::C_BG);
        ui::textCentered(y + 16, std::to_string(s.percent) + " %", ui::C_TEXT);
    }

    // Release notes are Markdown: drop the markup that would only clutter the small screen.
    static std::string notes(const std::string& md) {
        std::string out;
        size_t i = 0;
        while (i <= md.size()) {
            size_t e = md.find('\n', i);
            if (e == std::string::npos) e = md.size();
            std::string l = md.substr(i, e - i);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            size_t h = 0;
            while (h < l.size() && l[h] == '#') h++;
            if (h) l = l.substr(l.find_first_not_of(' ', h) == std::string::npos ? l.size() : l.find_first_not_of(' ', h));
            if (l.rfind("* ", 0) == 0 || l.rfind("- ", 0) == 0) l = "- " + l.substr(2);
            std::string clean;
            for (size_t k = 0; k < l.size(); k++)
                if (l[k] != '*' && l[k] != '`') clean += l[k];
            if (clean.find("Full Changelog") == std::string::npos) out += clean + "\n";
            i = e + 1;
        }
        return out;
    }
};
