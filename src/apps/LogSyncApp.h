#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/LogUpload.h"
#include "core/Ui.h"

// Sends the logbook to eQSL, Club Log and the QRZ.com logbook, and writes a
// Cabrillo file for contests.
class LogSyncApp : public App {
public:
    const char* name() const override { return T("Log upload"); }
    uint16_t color() const override { return 0x5AFF; }

    void onEnter() override {
        recount_ = true;
        message_.clear();
    }

    void onTick(uint32_t) override {
        if (recount_ && !dirty) {
            recount_ = false;
            counts_ = logupload::count();
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (ui::listNav(k, selected_, kRows)) { dirty = true; return; }
        bool again = k.ch == 'a' || k.ch == 'A';
        if (!k.enter && !again) return;
        self = this;
        if (selected_ < logupload::COUNT) {
            logupload::Service s = (logupload::Service)selected_;
            progress_.clear();
            draw();
            logupload::Result r = logupload::upload(s, again, [](int done, int total) {
                char b[32];
                snprintf(b, sizeof(b), "%d / %d", done, total);
                self->progress_ = b;
                self->drawProgress();
            });
            char b[96];
            snprintf(b, sizeof(b), "%s: %d %s, %d %s, %d %s", logupload::name(s), r.sent, T("sent"), r.dupes, T("already there"),
                     r.failed, T("refused"));
            message_ = r.error.empty() ? std::string(b) : (r.sent || r.dupes ? std::string(b) + ". " : "") + r.error;
            recount_ = true;
        } else {
            size_t n = 0;
            std::string err;
            int hours = again ? 0 : 48;
            if (logupload::exportCabrillo(hours, n, err)) {
                char b[96];
                snprintf(b, sizeof(b), "%u %s -> %s", (unsigned)n, T("contacts"), logupload::kCabrilloPath);
                message_ = b;
            } else {
                message_ = err;
            }
        }
        progress_.clear();
        dirty = true;
    }

    void draw() override {
        ui::clearBody();
        int y = ui::BODY_Y + 2;
        for (int i = 0; i < kRows; i++) {
            bool sel = i == selected_;
            if (sel) ui::fill(0, y - 1, ui::W, 12, ui::C_SEL);
            std::string label, value;
            uint16_t c = ui::C_TEXT;
            if (i < logupload::COUNT) {
                logupload::Service s = (logupload::Service)i;
                label = logupload::name(s);
                if (!logupload::configured(s)) {
                    value = std::string(T("set")) + " " + logupload::missingSetting(s);
                    c = ui::C_DIM;
                } else if (recount_) {
                    value = "...";
                } else {
                    value = std::to_string(counts_.waiting[i]) + " " + T("new");
                    c = counts_.waiting[i] ? ui::C_WARN : ui::C_OK;
                }
            } else {
                label = "Cabrillo";
                value = T("contest file, last 48 h");
            }
            ui::text(4, y, label, sel ? ui::C_TEXT : ui::C_NOTICE);
            ui::text(80, y, value, sel ? ui::C_TEXT : c);
            y += 14;
        }
        if (!recount_) {
            ui::text(4, y + 2, std::string(T("Contacts in the log")) + ": " + std::to_string(counts_.total), ui::C_DIM);
        }
        int my = y + 16;
        for (const std::string& l : ui::wrapLines(progress_.empty() ? message_ : progress_, 38)) {
            ui::text(4, my, l, ui::C_OK);
            my += 11;
            if (my > ui::H - ui::FOOTER_H - 10) break;
        }
        ui::footer(selected_ < logupload::COUNT ? T("ENTER send new  A send all") : T("ENTER last 48 h  A whole log"));
    }

private:
    static const int kRows = logupload::COUNT + 1;
    logupload::Counts counts_;
    int selected_ = 0;
    bool recount_ = false;
    std::string message_, progress_;
    static inline LogSyncApp* self = nullptr;

    void drawProgress() {
        ui::fill(0, 100, ui::W, 12, ui::C_BG);
        ui::text(4, 100, std::string(T("Sending")) + " " + progress_, ui::C_WARN);
    }
};
