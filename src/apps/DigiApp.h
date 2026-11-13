#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Ui.h"
#include "lib/CwDecoder.h"
#include "lib/Rtty.h"

// Decodes Morse code and RTTY heard by the microphone: hold the Cardputer
// next to the receiver's speaker. TAB switches between the two modes.
class DigiApp : public App {
public:
    const char* name() const override { return "Decoder"; }
    uint16_t color() const override { return 0xFBE0; }

    void onEnter() override {
        tone_ = atoi(config::get("cw_rx_tone", "700").c_str());
        if (tone_ < 300 || tone_ > 1200) tone_ = 700;
        restart();
    }

    void onExit() override {
        audio::stopCapture();
        if (toneChanged_) {
            config::set("cw_rx_tone", std::to_string(tone_));
            config::save();
            toneChanged_ = false;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) {
            mode_ = mode_ == CW ? RTTY : CW;
            text_.clear();
            restart();
        } else if (k.del) {
            text_.clear();
        } else if (mode_ == CW && (k.left() || k.right())) {
            tone_ += k.right() ? 50 : -50;
            if (tone_ < 300) tone_ = 300;
            if (tone_ > 1200) tone_ = 1200;
            cw_.setTone(tone_);
            toneChanged_ = true;
        } else if (mode_ == RTTY && (k.ch == 'r' || k.ch == 'R')) {
            rtty_.reverse = !rtty_.reverse;
        } else if (mode_ == RTTY && k.enter && !sending_) {
            std::string call = config::get("callsign");
            if (call.empty()) { status_ = "Set your callsign in Settings first"; dirty = true; return; }
            // Plays a CQ call through the speaker: hold it to the transmitter's microphone.
            delete tx_;
            tx_ = new rtty::Modulator("RYRYRY CQ CQ CQ DE " + call + " " + call + " " + call + " K");
            audio::play([](int16_t* out, size_t max, void* self) { return ((DigiApp*)self)->tx_->read(out, max); },
                        this, rtty::SAMPLE_RATE);
            sending_ = true;
            status_ = "Sending CQ through the speaker...";
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
            restart();
            dirty = true;
            return;
        }
        const int16_t* block = audio::capture();
        if (block) {
            std::string& fresh = mode_ == CW ? cw_.text : rtty_.text;
            if (mode_ == CW) cw_.feed(block, audio::BLOCK);
            else rtty_.feed(block, audio::BLOCK);
            if (!fresh.empty()) {
                text_ += fresh;
                fresh.clear();
                if (text_.size() > 600) text_.erase(0, text_.size() - 500);
                dirty = true;
            }
        }
        if (now - lastMs_ >= 200) {     // keep the level meter moving
            lastMs_ = now;
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        char line[64];
        if (mode_ == CW) snprintf(line, sizeof(line), "CW  %d Hz  %d WPM", tone_, cw_.wpm());
        else snprintf(line, sizeof(line), "RTTY 45 baud  170 Hz%s", rtty_.reverse ? "  REVERSE" : "");
        ui::text(6, ui::BODY_Y + 3, line, ui::C_SEL);

        float level = mode_ == CW ? cw_.level : rtty_.level;
        if (level < 0) level = 0;
        if (level > 1) level = 1;
        ui::fill(6, ui::BODY_Y + 15, ui::W - 12, 5, ui::C_FOOT);
        ui::fill(6, ui::BODY_Y + 15, (int)((ui::W - 12) * level), 5, (mode_ == CW && cw_.keyDown) ? ui::C_OK : ui::C_DIM);

        if (!micOk_) {
            ui::textCentered(ui::BODY_Y + 50, "Microphone not available", ui::C_WARN);
        } else if (text_.empty()) {
            ui::textCentered(ui::BODY_Y + 46, "Listening...", ui::C_DIM, 1.5);
            ui::textCentered(ui::BODY_Y + 68, "Hold the microphone to the radio's speaker", ui::C_DIM);
        } else {
            // The last seven lines of the decoded text.
            std::vector<std::string> lines = ui::wrapLines(text_, 38);
            const size_t rows = 7;
            size_t first = lines.size() > rows ? lines.size() - rows : 0;
            for (size_t i = first; i < lines.size(); i++)
                ui::text(6, ui::BODY_Y + 24 + (int)(i - first) * ui::ROW_H, lines[i], ui::C_TEXT);
        }
        if (!status_.empty()) ui::footer(status_.c_str());
        else if (mode_ == CW) ui::footer("TAB RTTY   , and / tone   DEL clear");
        else ui::footer("TAB CW  R reverse  ENTER CQ  DEL clear");
    }

private:
    enum Mode { CW, RTTY };

    void restart() {
        cw_ = cw::Decoder(tone_);
        bool reverse = rtty_.reverse;
        rtty_ = rtty::Demodulator();
        rtty_.reverse = reverse;
        micOk_ = audio::startCapture(mode_ == CW ? cw::SAMPLE_RATE : rtty::SAMPLE_RATE);
    }

    Mode mode_ = CW;
    cw::Decoder cw_;
    rtty::Demodulator rtty_;
    rtty::Modulator* tx_ = nullptr;
    std::string text_, status_;
    int tone_ = 700;
    bool micOk_ = false, sending_ = false, toneChanged_ = false;
    uint32_t lastMs_ = 0;
};
