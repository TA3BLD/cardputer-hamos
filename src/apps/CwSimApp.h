#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/Ui.h"

// CW QSO practice: a simulated station calls CQ in Morse (sidetone only, the
// radio is never keyed). Type its callsign + ENTER to answer; it sends you a
// report, name and town - type them + ENTER; it signs off. Score per contact,
// speed with = and -. ENTER on an empty line repeats the last transmission.
class CwSimApp : public App {
public:
    const char* name() const override { return T("CW practice"); }
    uint16_t color() const override { return 0xEF5D; }

    void onEnter() override {
        input_.upper = true;
        input_.maxLen = 30;
        newStation();
    }

    void onExit() override { keyer::stop(); }

    void onTick(uint32_t now) override {
        if (stage_ == WAIT_CQ && !keyer::busy() && now - doneMs_ > 1500 && doneMs_) {
            doneMs_ = 0;
            newStation();
        }
        if (now - drawMs_ > 300) { drawMs_ = now; dirty = true; }
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.ch == '=' || k.ch == '-') {
            if (!input_.value.empty()) { input_.handle(k); return; }
            keyer::setWpm(keyer::wpm() + (k.ch == '=' ? 2 : -2));
            return;
        }
        if (!k.enter) {
            input_.handle(k);
            return;
        }
        std::string in = input_.value;
        input_.value.clear();
        if (in.empty()) {                              // repeat
            keyer::send(last_, true);
            return;
        }
        if (stage_ == CALL) {
            bool ok = in == call_;
            result_ = ok ? T("Right!") : std::string(T("It was")) + " " + call_;
            score_ += ok ? 1 : 0;
            tries_++;
            stage_ = EXCHANGE;
            say(std::string(myCall()) + " DE " + call_ + " TNX FER CALL UR RST " + rst_ + " " + rst_ + " NAME " + name_ + " " +
                name_ + " QTH " + qth_ + " " + qth_ + " HW? " + myCall() + " DE " + call_ + " KN");
        } else if (stage_ == EXCHANGE) {
            int got = 0;
            for (const std::string& w : words(in)) got += w == rst_ || w == name_ || w == qth_;
            result_ = std::to_string(got) + "/3: RST " + rst_ + "  " + name_ + "  " + qth_;
            score_ += got == 3 ? 1 : 0;
            tries_++;
            stage_ = WAIT_CQ;
            say("R TNX " + name_ + " 73 GL " + myCall() + " DE " + call_ + " TU E E");
            doneMs_ = millis() + 1;
        }
    }

    void draw() override {
        ui::clearBody();
        char head[64];
        snprintf(head, sizeof(head), "%d WPM   %s %d/%d", keyer::wpm(), T("score"), score_, tries_);
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, head, ui::C_NOTICE);
        int y = ui::BODY_Y + 18;
        const char* ask = stage_ == CALL ? T("Who is calling CQ? Type the call") : stage_ == EXCHANGE ? T("Type RST, name and QTH")
                                                                                                     : T("Next station...");
        ui::text(4, y, ask, ui::C_TEXT);
        y += 14;
        ui::text(4, y, std::string("> ") + input_.value + "_", ui::C_OK, 1.5);
        y += 22;
        if (!result_.empty()) ui::text(4, y, result_, result_.find(T("Right!")) == 0 || result_.compare(0, 3, "3/3") == 0 ? ui::C_OK : ui::C_WARN);
        y += 14;
        if (keyer::busy()) ui::text(4, y, T("(listening...)"), ui::C_DIM);
        ui::footer(T("ENTER answer (empty: repeat)  =/- WPM"));
    }

    // For the tests
    const std::string& callForTest() const { return call_; }
    std::string exchangeForTest() const { return rst_ + " " + name_ + " " + qth_; }
    int scoreForTest() const { return score_; }

private:
    enum Stage { CALL, EXCHANGE, WAIT_CQ };
    Stage stage_ = CALL;
    ui::TextInput input_;
    std::string call_, rst_, name_, qth_, last_, result_;
    int score_ = 0, tries_ = 0;
    uint32_t doneMs_ = 0, drawMs_ = 0;

    static std::string myCall() {
        std::string c = config::get("callsign");
        return c.empty() ? "TA0XXX" : c;
    }

    static std::vector<std::string> words(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s + " ") {
            if (c == ' ' || c == ',') {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        return out;
    }

    void say(const std::string& text) {
        last_ = text;
        keyer::send(text, true);
    }

    void newStation() {
        static const char* prefixes[] = {"TA", "TA", "YM", "DL", "G", "F", "I", "EA", "OK", "SP", "UA", "JA", "K", "W", "VE", "PY", "VK", "ZS", "LZ", "SV", "YO", "HA", "S5", "9A", "OH"};
        static const char* names[] = {"ALI", "AYSE", "MEHMET", "HANS", "JOHN", "PIERRE", "LUCA", "JOSE", "PETR", "JAN", "IVAN", "TARO", "BOB", "MIKE", "ANA", "KEN"};
        static const char* towns[] = {"ANKARA", "IZMIR", "BURSA", "BERLIN", "LONDON", "PARIS", "ROMA", "MADRID", "PRAHA", "WARSAW", "MOSCOW", "TOKYO", "BOSTON", "DENVER", "SOFIA", "ATHENS"};
        static const char* rsts[] = {"599", "579", "559", "589", "449", "569"};
        std::string p = prefixes[rand() % 25];
        call_ = p + std::to_string(rand() % 10);
        int suffix = 1 + rand() % 3;
        for (int i = 0; i < suffix; i++) call_ += (char)('A' + rand() % 26);
        name_ = names[rand() % 16];
        qth_ = towns[rand() % 16];
        rst_ = rsts[rand() % 6];
        stage_ = CALL;
        result_.clear();
        say("CQ CQ CQ DE " + call_ + " " + call_ + " " + call_ + " K");
    }
};
