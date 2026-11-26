#pragma once
#include <cstdio>
#include <string>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Rig.h"
#include "core/Ui.h"
#include "core/Voice.h"

// Voice keyer: four recorded messages. 1-4 transmits one (PTT on, played on
// the speaker), R then 1-4 records, D then 1-4 then Y deletes, L repeats
// message 1 with a pause for answers (a calling-CQ loop) until any key.
class VoiceApp : public App {
public:
    const char* name() const override { return T("Voice keyer"); }
    uint16_t color() const override { return 0xFA60; }

    void onExit() override {
        voice::stopRecording();
        voice::stop();
        looping_ = false;
    }

    void onTick(uint32_t now) override {
        if (voice::recording() || voice::playing()) {
            if (now - lastDraw_ > 150) { lastDraw_ = now; dirty = true; }
            wasActive_ = true;
            return;
        }
        if (wasActive_) {
            wasActive_ = false;
            pauseFrom_ = now;
            dirty = true;
        }
        if (looping_) {
            int gap = atoi(config::get("voice_gap", "8").c_str());
            if (gap < 2) gap = 2;
            if (now - pauseFrom_ >= (uint32_t)gap * 1000) voice::play(1, true);
            if (now - lastDraw_ > 500) { lastDraw_ = now; dirty = true; }
        }
    }

    bool onBack() override {
        if (voice::recording() || voice::playing() || looping_ || armed_ || deleteArmed_ || deleteSlot_) {
            voice::stopRecording();
            voice::stop();
            looping_ = armed_ = deleteArmed_ = false;
            deleteSlot_ = 0;
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (voice::recording()) {               // any key ends the recording
            voice::stopRecording();
            dirty = true;
            return;
        }
        if (looping_ || voice::playing()) {     // any key stops transmitting
            voice::stop();
            looping_ = false;
            dirty = true;
            return;
        }
        if (deleteSlot_) {                      // "Delete n? Y/N"
            if (k.ch == 'y' || k.ch == 'Y') message_ = voice::erase(deleteSlot_) ? T("Deleted") : T("Could not delete");
            else message_.clear();
            deleteSlot_ = 0;
            dirty = true;
            return;
        }
        if (deleteArmed_ && k.ch >= '1' && k.ch <= '4') {
            deleteArmed_ = false;
            int n = k.ch - '0';
            if (voice::exists(n)) {
                deleteSlot_ = n;
                message_.clear();
            } else {
                message_ = T("Nothing recorded there yet");
            }
            dirty = true;
            return;
        }
        if (k.ch == 'd' || k.ch == 'D' || k.del) {
            deleteArmed_ = true;
            armed_ = false;
            message_.clear();
            dirty = true;
            return;
        }
        deleteArmed_ = false;
        if (k.ch == 'r' || k.ch == 'R') { armed_ = true; dirty = true; return; }
        if (k.ch >= '1' && k.ch <= '4') {
            int n = k.ch - '0';
            if (armed_) {
                armed_ = false;
                message_ = voice::startRecording(n) ? "" : T("Cannot record (SD card?)");
            } else if (!voice::play(n, true)) {
                message_ = T("Nothing recorded there yet");
            } else {
                message_.clear();
            }
            dirty = true;
            return;
        }
        if ((k.ch == 'l' || k.ch == 'L') && voice::exists(1)) {
            looping_ = true;
            voice::play(1, true);
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        std::string head = rig::pttAvailable() ? std::string("PTT: ") + (*rig::typeName() ? rig::typeName() : "Grove") : T("No PTT: use VOX");
        ui::text(4, ui::BODY_Y + 2, head, ui::C_NOTICE);
        int y = ui::BODY_Y + 16;
        for (int i = 1; i <= voice::kMemories; i++) {
            char b[48];
            float len = voice::length(i);
            if (len > 0) snprintf(b, sizeof(b), "%d  %4.1f s", i, len);
            else snprintf(b, sizeof(b), "%d  %s", i, T("(empty)"));
            ui::text(6, y, b, len > 0 ? ui::C_TEXT : ui::C_DIM, 1.5);
            y += 17;
        }
        if (voice::recording()) {
            char b[32];
            snprintf(b, sizeof(b), "REC %.1f s", voice::seconds());
            ui::text(130, ui::BODY_Y + 20, b, ui::C_ERR, 1.5);
            ui::frame(130, ui::BODY_Y + 42, 100, 10, ui::C_TEXT);
            ui::fill(131, ui::BODY_Y + 43, voice::level() * 98 / 100, 8, voice::level() > 90 ? ui::C_ERR : ui::C_OK);
            ui::footer(T("Speak now; any key stops"));
            return;
        }
        if (voice::playing()) {
            ui::text(150, ui::BODY_Y + 30, "TX", ui::C_ERR, 3);
            ui::footer(T("Any key stops"));
            return;
        }
        if (looping_) {
            ui::text(130, ui::BODY_Y + 30, T("Listening..."), ui::C_OK);
            ui::footer(T("Any key stops the CQ loop"));
            return;
        }
        if (deleteSlot_) {
            char b[32];
            snprintf(b, sizeof(b), "%s %d?", T("Delete"), deleteSlot_);
            ui::text(130, ui::BODY_Y + 30, b, ui::C_ERR, 1.5);
            ui::footer(T("Y delete   any other key keeps it"));
            return;
        }
        if (armed_) ui::text(130, ui::BODY_Y + 30, T("Record: 1-4?"), ui::C_WARN);
        else if (deleteArmed_) ui::text(130, ui::BODY_Y + 30, T("Delete: 1-4?"), ui::C_WARN);
        else if (!message_.empty()) ui::text(130, ui::BODY_Y + 30, message_, ui::C_WARN);
        ui::footer(T("1-4 send R rec D del L CQ loop"));
    }

private:
    bool armed_ = false, looping_ = false, wasActive_ = false, deleteArmed_ = false;
    int deleteSlot_ = 0;                    // waiting for Y to delete this one
    uint32_t lastDraw_ = 0, pauseFrom_ = 0;
    std::string message_;
};
