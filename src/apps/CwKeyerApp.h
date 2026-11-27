#pragma once
#include <cstdio>
#include <string>
#include "core/App.h"
#include "core/Config.h"
#include "core/Keyer.h"
#include "core/Lang.h"
#include "core/Rig.h"
#include "core/Ui.h"
#include "apps/CwView.h"

// CW keyer: six memories (1-6, or Fn+1-6 while typing), typed text (ENTER
// sends), speed with = and -. TAB edits the other station's callsign used in
// the memories. A paddle (Settings "Paddle ...") works at the same time.
// While sending, a TX line shows the letter and the dot or dash going out.
class CwKeyerApp : public App {
public:
    const char* name() const override { return T("CW keyer"); }
    uint16_t color() const override { return 0xFFE0; }

    void onEnter() override {
        call_.upper = true;
        call_.maxLen = 12;
        text_.upper = true;
        text_.maxLen = 40;
        nr_ = atoi(config::get("cw_nr", "1").c_str());
        keyer::setKeyerPage(true);      // here the paddle always keys the radio
    }

    void onExit() override { keyer::setKeyerPage(false); }

    void onTick(uint32_t now) override {
        bool b = keyer::busy();
        // Redraw when what is sent or keyed moves on (each dot and dash).
        keyer::TxView v = keyer::txView();
        bool down = false;
        std::string live = keyer::paddlePattern(&down);
        char sig[48];
        snprintf(sig, sizeof(sig), "%d%d%u/%d%d%d", v.active, v.doneMsAgo < 3000, (unsigned)v.letter, v.element, v.down, down);
        std::string p = keyer::paddleText() + "|" + live + sig;
        if (b != wasBusy_ || p != shownPaddle_ || now - lastDraw_ > 1000) {
            wasBusy_ = b;
            shownPaddle_ = p;
            lastDraw_ = now;
            dirty = true;
        }
    }

    bool onBack() override {
        if (keyer::busy()) { keyer::stop(); dirty = true; return true; }
        if (editCall_) { editCall_ = false; dirty = true; return true; }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab) { editCall_ = !editCall_; dirty = true; return; }
        if (k.ch >= '1' && k.ch <= '6' && (k.fn || (!editCall_ && text_.value.empty()))) {
            sendMemory(k.ch - '0');
            return;
        }
        if (!editCall_ && (k.ch == '=' || k.ch == '-') && text_.value.empty()) {
            keyer::setWpm(keyer::wpm() + (k.ch == '=' ? 1 : -1));
            dirty = true;
            return;
        }
        if (k.enter) {
            if (editCall_) { editCall_ = false; dirty = true; return; }
            if (!text_.value.empty()) {
                keyer::send(text_.value);
                last_ = text_.value;
                text_.value.clear();
                dirty = true;
            }
            return;
        }
        if ((editCall_ ? call_ : text_).handle(k)) dirty = true;
    }

    void draw() override {
        ui::clearBody();
        char head[64];
        snprintf(head, sizeof(head), "%d WPM  > %s", keyer::wpm(), keyer::outputName());
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, head, ui::C_NOTICE);
        if (keyer::busy()) ui::text(ui::W - 24, ui::BODY_Y + 2, "TX", ui::C_ERR);
        int y = ui::BODY_Y + 14;
        for (int i = 1; i <= 6; i++) {
            std::string m = keyer::expand(keyer::memory(i), call_.value.empty() ? "?" : call_.value, nrText());
            ui::text(2, y, std::to_string(i), ui::C_NOTICE);
            ui::text(12, y, m.substr(0, 37), ui::C_TEXT);
            y += 10;
        }
        y += 2;
        ui::text(2, y, T("Call"), editCall_ ? ui::C_NOTICE : ui::C_DIM);
        ui::text(32, y, call_.value + (editCall_ ? "_" : ""), editCall_ ? ui::C_TEXT : ui::C_OK);
        ui::text(120, y, T("NR ") + nrText(), ui::C_DIM);
        y += 11;
        bool down = false;
        std::string live = keyer::paddlePattern(&down);
        if (cwview::tx(y, keyer::txView())) {
            // the TX line and its dots and dashes
        } else if (keyer::paddle() && (!keyer::paddleText().empty() || !live.empty())) {
            std::string p = keyer::paddleText();
            ui::text(2, y, "~ " + p.substr(p.size() > 36 ? p.size() - 36 : 0), ui::C_OK);
            // the letter being keyed, as it is keyed
            cwview::live(14, y + 11, live, down, 1);
        } else {
            ui::text(2, y, "> " + text_.value + (editCall_ ? "" : "_"), ui::C_TEXT);
        }
        ui::footer(T("1-6 mem  ENTER send  =/- WPM  TAB call"));
    }

private:
    ui::TextInput call_, text_;
    bool editCall_ = false, wasBusy_ = false;
    std::string last_, shownPaddle_;
    uint32_t lastDraw_ = 0;
    int nr_ = 1;

    std::string nrText() const {
        char b[8];
        snprintf(b, sizeof(b), "%03d", nr_);
        return b;
    }

    void sendMemory(int n) {
        std::string m = keyer::memory(n);
        keyer::send(keyer::expand(m, call_.value, nrText()));
        if (m.find("{NR}") != std::string::npos) {      // the serial number counts up once it is sent
            nr_++;
            config::set("cw_nr", std::to_string(nr_));
        }
        dirty = true;
    }
};
