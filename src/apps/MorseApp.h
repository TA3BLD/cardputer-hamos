#pragma once
#include <M5Cardputer.h>
#include <cstdlib>
#include <string>
#include "core/App.h"
#include "core/Config.h"
#include "core/Ui.h"
#include "lib/Morse.h"

// CW practice. SEND: type and hear it. TRAIN: Koch method - a character is
// played, you type what you heard; the level rises once you reach 90 %.
class MorseApp : public App {
public:
    const char* name() const override { return "Morse"; }
    uint16_t color() const override { return 0xFFE0; }

    void onEnter() override {
        wpm_ = atoi(config::get("cw_wpm", "18").c_str());
        if (wpm_ < 5 || wpm_ > 50) wpm_ = 18;
        tone_ = atoi(config::get("cw_tone", "700").c_str());
        if (tone_ < 200 || tone_ > 2000) tone_ = 700;
        level_ = atoi(config::get("koch_level", "2").c_str());
        if (level_ < 2) level_ = 2;
        if (level_ > kMaxLevel) level_ = kMaxLevel;
        srand((unsigned)millis());
        queue_.clear();
        sent_.clear();
        training_ = false;
    }

    void onExit() override { queue_.clear(); }

    void onKey(const KeyEvent& k) override {
        if (k.tab) {
            training_ = !training_;
            queue_.clear();
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
            bool ok = toupper((unsigned char)k.ch) == target_;
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
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (training_ && answered_ && now >= askAt_) {
            target_ = morse::kKochOrder[rand() % level_];
            answered_ = false;
            play(target_);
            dirty = true;
        }
        if (queue_.empty() || now < nextAt_) return;
        char el = queue_[0];
        queue_.erase(0, 1);
        int dot = morse::dotMs(wpm_);
        if (el == '.' || el == '-') {
            int len = (el == '.') ? dot : 3 * dot;
            M5Cardputer.Speaker.tone(tone_, len);
            nextAt_ = now + len + dot;
        } else {
            nextAt_ = now + (el == ' ' ? 2 : 4) * dot;   // character gap 3 dots, word gap 7
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
            if (!sent_.empty() && sent_.back() != ' ')
                ui::textCentered(ui::BODY_Y + 66, morse::encode(sent_.back()), ui::C_OK, 3);
            else
                ui::textCentered(ui::BODY_Y + 72, "Type to send", ui::C_DIM);
            ui::footer("TAB trainer   DEL clear   ` back");
        } else {
            snprintf(line, sizeof(line), "TRAIN  level %d/%d", level_, kMaxLevel);
            ui::text(6, ui::BODY_Y + 6, line, ui::C_SEL, 1.5);
            ui::text(6, ui::BODY_Y + 24, std::string(morse::kKochOrder).substr(0, level_), ui::C_DIM);
            if (answered_ && !result_.empty())
                ui::textCentered(ui::BODY_Y + 46, result_, resultOk_ ? ui::C_OK : ui::C_ERR, 2);
            else
                ui::textCentered(ui::BODY_Y + 42, "?", ui::C_TEXT, 4);
            int pct = total_ ? (100 * correct_ / total_) : 0;
            snprintf(line, sizeof(line), "%d / %d correct  (%d%%)   %d WPM", correct_, total_, pct, wpm_);
            ui::text(6, ui::BODY_Y + 88, line, ui::C_DIM);
            ui::footer("Type answer  ENTER replay  TAB send");
        }
    }

private:
    static constexpr int kMaxLevel = 40;

    void play(char c) { queue_ += std::string(morse::encode(c)) + " "; }
    void nextQuestion(uint32_t at) {
        askAt_ = at;
        answered_ = true;   // "waiting for the next question"
    }

    std::string queue_, sent_, result_;
    uint32_t nextAt_ = 0, askAt_ = 0;
    int wpm_ = 18, tone_ = 700, level_ = 2, correct_ = 0, total_ = 0;
    bool training_ = false, answered_ = true, resultOk_ = false;
    char target_ = 'K';
};
