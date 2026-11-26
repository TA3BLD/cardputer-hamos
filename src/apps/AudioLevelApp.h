#pragma once
#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "core/App.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Rig.h"
#include "core/Ui.h"

// Audio levels for the digital modes.
//   Receive: the microphone's level while the radio plays noise / signals:
//            aim for the green zone, never red.
//   Send (TAB): a steady 1500 Hz tone (P: with PTT) to set the radio's mic
//            gain / ALC; = and - change the speaker volume.
class AudioLevelApp : public App {
public:
    const char* name() const override { return T("Audio levels"); }
    uint16_t color() const override { return 0x9E66; }

    void onEnter() override {
        sending_ = false;
        audio::startCapture(8000);
    }

    void onExit() override {
        stopTone();
        if (audio::capturing()) audio::stopCapture();
    }

    void onTick(uint32_t now) override {
        if (!sending_) {
            if (const int16_t* b = audio::capture()) {
                double sum = 0;
                int peak = 0;
                for (size_t i = 0; i < audio::BLOCK; i++) {
                    sum += (double)b[i] * b[i];
                    peak = std::max(peak, abs((int)b[i]));
                }
                double rms = std::sqrt(sum / audio::BLOCK);
                double db = rms > 0 ? 20 * std::log10(rms / 32768.0) : -90;
                rmsDb_ = db > rmsDb_ ? db : rmsDb_ * 0.9 + db * 0.1;      // rises at once, falls slowly
                peakDb_ = peak > 0 ? 20 * std::log10(peak / 32768.0) : -90;
                if (peakDb_ > holdDb_ || now - holdMs_ > 1500) {
                    holdDb_ = peakDb_;
                    holdMs_ = now;
                }
            }
        }
        if (now - drawMs_ > 120) {
            drawMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.tab) {
            sending_ = !sending_;
            if (sending_) {
                audio::stopCapture();
                startTone();
            } else {
                stopTone();
                audio::startCapture(8000);
            }
            return;
        }
        if (k.ch == '=' || k.ch == '-') {
            int v = atoi(config::get("volume", "160").c_str()) + (k.ch == '=' ? 10 : -10);
            v = v < 10 ? 10 : v > 255 ? 255 : v;
            config::set("volume", std::to_string(v));
            config::save();
            M5Cardputer.Speaker.setVolume((uint8_t)v);
        }
        if (sending_ && (k.ch == 'p' || k.ch == 'P')) {
            ptt_ = !ptt_;
            rig::ptt(ptt_);
        }
    }

    void draw() override {
        ui::clearBody();
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, sending_ ? T("Send level (tone 1500 Hz)") : T("Receive level (microphone)"), ui::C_NOTICE);
        int y = ui::BODY_Y + 18;
        if (!sending_) {
            // -60 dB .. 0 dB across the bar
            auto xOf = [](double db) { return 10 + (int)((db < -60 ? 0 : db > 0 ? 60 : db + 60) * 220 / 60); };
            ui::fill(10, y, xOf(-40) - 10, 16, 0x4208);
            ui::fill(xOf(-40), y, xOf(-10) - xOf(-40), 16, 0x0320);
            ui::fill(xOf(-10), y, xOf(-3) - xOf(-10), 16, 0x6300);
            ui::fill(xOf(-3), y, 230 - xOf(-3), 16, 0x6000);
            ui::fill(10, y + 4, xOf(rmsDb_) - 10, 8, ui::C_TEXT);
            ui::fill(xOf(holdDb_) - 1, y, 2, 16, ui::C_NOTICE);
            y += 22;
            char b[64];
            snprintf(b, sizeof(b), "RMS %.0f dB   %s %.0f dB", rmsDb_, T("peak"), holdDb_);
            ui::text(10, y, b, ui::C_TEXT);
            y += 14;
            const char* advice = holdDb_ > -3 ? T("Too loud: clipping. Turn the radio down.")
                               : rmsDb_ < -45 ? T("Too quiet: turn the radio up.")
                               : rmsDb_ > -12 ? T("A little loud: noise should sit lower.")
                                              : T("Good level for FT8, SSTV, PSK31, APRS.");
            ui::text(10, y, advice, holdDb_ > -3 ? ui::C_ERR : (rmsDb_ < -45 || rmsDb_ > -12) ? ui::C_WARN : ui::C_OK);
            ui::footer(T("TAB send test  =/- speaker volume"));
        } else {
            char b[64];
            snprintf(b, sizeof(b), "%s %s", T("Speaker volume"), config::get("volume", "160").c_str());
            ui::text(10, y, b, ui::C_TEXT, 1.5);
            y += 22;
            ui::text(10, y, ptt_ ? T("PTT on: transmitting") : T("PTT off (P switches)"), ptt_ ? ui::C_ERR : ui::C_DIM);
            y += 14;
            for (const std::string& l : ui::wrapLines(T("Raise the radio's mic gain until ALC just starts to move, then back off a little."), 38)) {
                ui::text(10, y, l, ui::C_DIM);
                y += 10;
            }
            ui::footer(T("TAB receive  P PTT  =/- volume"));
        }
    }

private:
    bool sending_ = false, ptt_ = false;
    double rmsDb_ = -90, peakDb_ = -90, holdDb_ = -90;
    uint32_t holdMs_ = 0, drawMs_ = 0;
    uint32_t phase_ = 0;

    static size_t tone(int16_t* out, size_t max, void* self) {
        AudioLevelApp* a = (AudioLevelApp*)self;
        for (size_t i = 0; i < max; i++) {
            out[i] = (int16_t)(12000 * std::sin(a->phase_ * 2 * M_PI * 1500 / 8000.0));
            a->phase_ = (a->phase_ + 1) % 16;           // 1500 Hz repeats every 16 samples at 8 kHz
        }
        return max;
    }
    void startTone() { audio::play(tone, this, 8000); }
    void stopTone() {
        if (audio::playing()) audio::stopPlaying();
        if (ptt_) rig::ptt(false);
        ptt_ = false;
    }
};
