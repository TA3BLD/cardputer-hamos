#pragma once
#include <M5Cardputer.h>
#include <cstdlib>
#include <string>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Config.h"
#include "core/Keyer.h"
#include "core/Ui.h"
#include "apps/CwView.h"
#include "lib/Morse.h"

// CW practice. SEND: type or key with the paddle and hear it. TRAIN: Koch
// method - a character is played, you type or key what you heard; the level
// rises once you reach 90 %. Its own speed ("morse_wpm", Fn + ; / . here);
// the paddle only gives sidetone here, it never keys the radio.
class MorseApp : public App {
public:
    const char* name() const override { return T("Morse"); }
    uint16_t color() const override { return 0xFFE0; }

    void onEnter() override {
        wpm_ = atoi(config::get("morse_wpm", config::get("cw_wpm", "18").c_str()).c_str());
        if (wpm_ < 5 || wpm_ > 50) wpm_ = 18;
        tone_ = atoi(config::get("cw_tone", "700").c_str());
        if (tone_ < 200 || tone_ > 2000) tone_ = 700;
        level_ = atoi(config::get("koch_level", "2").c_str());
        if (level_ < 2) level_ = 2;
        if (level_ > kMaxLevel) level_ = kMaxLevel;
        srand((unsigned)millis());
        stopPlay();
        sent_.clear();
        training_ = false;
        keyer::setPractice(true, wpm_);
        keyer::takePaddleLetters();
    }

    void onExit() override {
        stopPlay();
        keyer::setPractice(false);
    }

    void onKey(const KeyEvent& k) override {
        if (k.fnUp() || k.fnDown()) {                  // Fn + ; / .  speed
            wpm_ += k.fnUp() ? 1 : -1;
            wpm_ = wpm_ < 5 ? 5 : wpm_ > 50 ? 50 : wpm_;
            config::set("morse_wpm", std::to_string(wpm_));
            config::save();
            keyer::setPractice(true, wpm_);
        } else if (k.tab) {
            training_ = !training_;
            stopPlay();
            if (training_) {
                correct_ = total_ = 0;
                result_.clear();
                nextQuestion(millis() + 500);
            }
        } else if (!training_) {
            if (k.del) sent_.clear();
            else if (k.ch == ' ') { sent_ += ' '; queue_ += "/"; }
            else if (k.ch && morse::encode(k.ch)[0]) { sent_ += (char)toupper((unsigned char)k.ch); play(k.ch); }
            else return;
        } else if (k.enter) {
            if (answered_) return;
            play(target_);                               // replay
        } else if (k.ch && morse::encode(k.ch)[0] && !answered_) {
            answer(k.ch);
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        // Letters keyed with the paddle: written down, or the answer in the trainer.
        for (char c : keyer::takePaddleLetters()) {
            if (!training_) {
                if (c == ' ' && (sent_.empty() || sent_.back() == ' ')) continue;
                sent_ += c;
                dirty = true;
            } else if (c != ' ' && c != '*' && !answered_) {
                answer(c);
                dirty = true;
            }
        }
        // What the paddle keys right now, and the end of a played tone: redraw.
        bool down = false;
        std::string live = keyer::paddlePattern(&down) + (down ? "*" : "") + (now < toneUntil_ ? "t" : "");
        if (live != shownLive_) {
            shownLive_ = live;
            dirty = true;
        }
        if (training_ && answered_ && now >= askAt_) {
            target_ = morse::kKochOrder[rand() % level_];
            answered_ = false;
            play(target_);
            dirty = true;
        }
        if (now < nextAt_) return;
        if (queue_.empty()) {
            if (!playing_.empty()) {        // all played
                stopPlay();
                dirty = true;
            }
            return;
        }
        char el = queue_[0];
        queue_.erase(0, 1);
        int dot = morse::dotMs(wpm_);
        if (el == '.' || el == '-') {
            int len = (el == '.') ? dot : 3 * dot;
            M5Cardputer.Speaker.tone(tone_, len);
            nextAt_ = now + len + dot;
            toneUntil_ = now + len;
            playEl_++;
            dirty = true;
        } else {
            nextAt_ = now + (el == ' ' ? 2 : 4) * dot;   // character gap 3 dots, word gap 7
            if (el == ' ') {                // the next letter
                playPos_++;
                playEl_ = -1;
            }
        }
    }

    void draw() override {
        ui::clearBody();
        char line[48];
        if (!training_) {
            ui::text(6, ui::BODY_Y + 6, "SEND", ui::C_SEL, 1.5);
            snprintf(line, sizeof(line), "%d WPM  %d Hz", wpm_, tone_);
            ui::text(130, ui::BODY_Y + 9, line, ui::C_DIM);
            std::string tail = sent_.size() > 18 ? sent_.substr(sent_.size() - 18) : sent_;
            ui::text(6, ui::BODY_Y + 32, tail + "_", ui::C_TEXT, 2);
            // Dots and dashes: what the paddle keys now, the letter being
            // played (the element sounding in red), or the last letter.
            bool down = false;
            std::string live = keyer::paddlePattern(&down);
            const int py = ui::BODY_Y + 66;
            if (!live.empty()) {
                cwview::live((ui::W - cwview::width(live, 2)) / 2, py, live, down, 2);
            } else if (playPos_ < playing_.size()) {
                cwview::centred(py, morse::encode(playing_[playPos_]), playEl_ < 0 ? 0 : playEl_, millis() < toneUntil_, 2, ui::C_OK);
                ui::textCentered(py + 14, std::string(1, playing_[playPos_]), ui::C_WARN);     // the letter playing
            } else if (!sent_.empty() && sent_.back() != ' ') {
                cwview::centred(py, morse::encode(sent_.back()), -1, false, 2, ui::C_OK);
                ui::textCentered(py + 14, morse::encode(sent_.back()), ui::C_DIM);
            } else {
                ui::textCentered(ui::BODY_Y + 72, T("Type or key to send"), ui::C_DIM);
            }
            paddleLine(ui::BODY_Y + 96);
            ui::footer(T("TAB train DEL clear Fn;. WPM ` back"));
        } else {
            snprintf(line, sizeof(line), "TRAIN  level %d/%d", level_, kMaxLevel);
            ui::text(6, ui::BODY_Y + 6, line, ui::C_SEL, 1.5);
            ui::text(6, ui::BODY_Y + 24, std::string(morse::kKochOrder).substr(0, level_), ui::C_DIM);
            bool down = false;
            std::string live = keyer::paddlePattern(&down);
            if (answered_ && !result_.empty())
                ui::textCentered(ui::BODY_Y + 46, result_, resultOk_ ? ui::C_OK : ui::C_ERR, 2);
            else if (!live.empty())         // the answer being keyed
                cwview::live((ui::W - cwview::width(live, 2)) / 2, ui::BODY_Y + 52, live, down, 2);
            else
                ui::textCentered(ui::BODY_Y + 42, "?", ui::C_TEXT, 4);
            int pct = total_ ? (100 * correct_ / total_) : 0;
            snprintf(line, sizeof(line), "%d / %d correct  (%d%%)   %d WPM", correct_, total_, pct, wpm_);
            ui::text(6, ui::BODY_Y + 84, line, ui::C_DIM);
            paddleLine(ui::BODY_Y + 96);
            ui::footer(T("Type/key answer ENTER replay TAB send"));
        }
    }

private:
    static constexpr int kMaxLevel = 40;

    void play(char c) {
        queue_ += std::string(morse::encode(c)) + " ";
        playing_ += (char)toupper((unsigned char)c);
    }
    void stopPlay() {
        queue_.clear();
        playing_.clear();
        playPos_ = 0;
        playEl_ = -1;
        toneUntil_ = 0;
    }

    void answer(char c) {
        bool ok = toupper((unsigned char)c) == target_;
        total_++;
        if (ok) correct_++;
        result_ = std::string(ok ? "OK  " : "NO  ") + target_ + "  " + morse::encode(target_);
        resultOk_ = ok;
        answered_ = true;
        if (total_ >= 20 && correct_ * 10 >= total_ * 9 && level_ < kMaxLevel) {
            level_++;
            config::set("koch_level", std::to_string(level_));
            config::save();
            correct_ = total_ = 0;
            result_ += "  LEVEL UP";
        }
        nextQuestion(millis() + 900);
    }

    // What the paddle is set to, or why it does not work; nothing when off.
    static void paddleLine(int y) {
        std::string info = keyer::paddleInfo();
        if (info.empty()) return;
        if (keyer::paddleError()) ui::text(6, y, info, ui::C_WARN);
        else ui::text(6, y, std::string(T("Paddle")) + " " + info, ui::C_DIM);
    }
    void nextQuestion(uint32_t at) {
        askAt_ = at;
        answered_ = true;   // "waiting for the next question"
    }

    std::string queue_, sent_, result_;
    std::string playing_, shownLive_;       // letters being played; what was drawn last
    size_t playPos_ = 0;                    // the letter playing now, and its element
    int playEl_ = -1;
    uint32_t toneUntil_ = 0;
    uint32_t nextAt_ = 0, askAt_ = 0;
    int wpm_ = 18, tone_ = 700, level_ = 2, correct_ = 0, total_ = 0;
    bool training_ = false, answered_ = true, resultOk_ = false;
    char target_ = 'K';
};
