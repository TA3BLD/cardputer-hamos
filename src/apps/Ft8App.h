#pragma once
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/PskReporter.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Flags.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Station.h"
#include "core/System.h"
#include "core/Ui.h"
#include "lib/Dxcc.h"
#include "lib/Ft8.h"
#include "lib/Ft8Decoder.h"
#include "lib/Ft8Qso.h"

// FT8 on the Cardputer itself: listens through the microphone, decodes every
// 15 second period, and answers or calls CQ through the speaker / headphone
// jack, running the standard contact sequence and logging the contact.
// Needs the time to within about half a second (WiFi/NTP or GPS).
class Ft8App : public App {
public:
    const char* name() const override { return "FT8"; }
    uint16_t color() const override { return 0xF81F; }

    void onEnter() override {
        qso_.myCall = config::get("callsign");
        qso_.myGrid = station::grid();
        status_.clear();
        logbook::load();
        // The finer frequency grid needs about 180 KB; keep at least 60 KB for everything else.
        int osr = sys::freeHeap() > ft8::Decoder::memoryNeeded(2) + 60000 ? 2 : 1;
        decoder_.reset(new ft8::Decoder());
        if (!decoder_->begin(osr) && !(osr == 2 && decoder_->begin(1))) {
            decoder_.reset();
            status_ = "Not enough memory for the decoder";
        }
        txHz_ = (float)atof(config::get("ft8_tx_hz", "1500").c_str());
        if (txHz_ < 300 || txHz_ > 2700) txHz_ = 1500;
        period_ = -1;
        rx_ = tx_ = decoding_ = false;
        follow_ = true;
        audio::startCapture(8000);
    }

    void onExit() override {
        audio::stopCapture();
        decoder_.reset();           // give the memory back
        tx_ = false;
        txEnabled_ = false;
        qso_.stop();
    }

    void onKey(const KeyEvent& k) override {
        if (k.up() || k.down()) {
            int count = (int)lines_.size();
            if (count == 0) return;
            selected_ += k.down() ? 1 : -1;
            if (selected_ < 0) selected_ = 0;
            follow_ = selected_ >= count - 1;
            if (follow_) selected_ = count - 1;
        } else if (k.enter) {
            answerSelected();
        } else if (k.ch == 'c' || k.ch == 'C') {
            if (!canTransmit()) return;
            qso_.callCq();
            txParity_ = (int)((period_ + 1) % 2);       // start in the next period
            txEnabled_ = true;
            status_.clear();
        } else if (k.ch == 't' || k.ch == 'T') {
            if (!txEnabled_ && qso_.message().empty()) { status_ = "Nothing to send: ENTER on a CQ, or C"; }
            else txEnabled_ = !txEnabled_;
        } else if (k.ch == 'h' || k.ch == 'H') {
            txEnabled_ = false;
            qso_.stop();
            status_ = "Halted";
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (!decoder_ || !net::timeValid()) {
            if (now - lastDrawMs_ > 1000) { lastDrawMs_ = now; dirty = true; }
            return;
        }
        long long t = net::utcMillis();
        long long period = t / 15000;
        int ms = (int)(t % 15000);

        if (period != period_) {
            finishDecoding();       // the last period's decodes decide what to send now
            period_ = period;
            startPeriod(ms);
        }
        // Enabled just after our period began (ENTER on a fresh decode): start now.
        if (txEnabled_ && !tx_ && lastTxPeriod_ != period_ && (int)(period_ % 2) == txParity_ && ms < 1500 &&
            !qso_.message().empty()) {
            rx_ = false;
            startTransmit(ms);
        }
        if (tx_ && !audio::playing() && ms > 13000) {
            tx_ = false;
            audio::startCapture(8000);
        }
        if (rx_) {
            for (int i = 0; i < 4; i++) {           // catch up if the loop was slow
                const int16_t* block = audio::capture();
                if (!block) break;
                int16_t resampled[audio::BLOCK];
                size_t n = resampler_.process(block, audio::BLOCK, resampled);
                decoder_->feed(resampled, n);
            }
            if (decoder_->full()) {
                rx_ = false;
                decoder_->startDecode();
                decoding_ = true;
                fresh_.clear();
            }
        }
        if (decoding_ && !decoder_->step(fresh_, 3, qso_.myCall)) finishDecoding();
        if (now - lastDrawMs_ > 500) {
            lastDrawMs_ = now;
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        if (!decoder_) {
            ui::textCentered(ui::BODY_Y + 40, status_, ui::C_WARN);
            ui::footer("` back");
            return;
        }
        if (!net::timeValid()) {
            ui::textCentered(ui::BODY_Y + 30, "Waiting for the time", ui::C_WARN, 1.5);
            ui::textCentered(ui::BODY_Y + 54, "FT8 needs it to within 0.5 s: WiFi or GPS", ui::C_DIM);
            ui::footer("` back");
            return;
        }
        // Top line: period progress, receive / transmit, dial frequency.
        int ms = (int)(net::utcMillis() % 15000);
        int barW = ui::W - 12;
        ui::fill(6, ui::BODY_Y + 2, barW, 4, ui::C_FOOT);
        ui::fill(6, ui::BODY_Y + 2, barW * ms / 15000, 4, tx_ ? ui::C_ERR : rx_ ? ui::C_OK : ui::C_DIM);
        char line[64];
        snprintf(line, sizeof(line), "%s %s MHz  %s  %s", tx_ ? "TX" : decoding_ ? "decoding" : rx_ ? "RX" : "--",
                 dialMhz().c_str(), (period_ % 2) ? "odd" : "even", decoder_->freqOsr() == 2 ? "" : "(low memory)");
        ui::text(6, ui::BODY_Y + 8, line, tx_ ? ui::C_ERR : ui::C_DIM);

        // Decodes, newest at the bottom.
        std::vector<std::string> rows;
        std::vector<uint16_t> colors;
        for (const Line& l : lines_) {
            if (l.mine) snprintf(line, sizeof(line), "TX   %4.0f %s", txHz_, l.text.c_str());
            else snprintf(line, sizeof(line), "%+3d %4.0f %-19.19s    %.6s", l.snr, l.freq, l.text.c_str(), l.country.c_str());
            rows.push_back(line);
            colors.push_back(l.mine ? ui::C_NOTICE : l.toMe ? ui::C_OK : (l.cq && l.newCountry) ? ui::C_WARN
                             : l.cq ? ui::C_TEXT : ui::C_DIM);
        }
        if (follow_ && !rows.empty()) selected_ = (int)rows.size() - 1;
        if (rows.empty()) {
            ui::textCentered(ui::BODY_Y + 46, rx_ || decoding_ ? "Listening..." : "Next period starts soon", ui::C_DIM);
            ui::textCentered(ui::BODY_Y + 62, "Microphone next to the receiver's speaker", ui::C_DIM);
        } else {
            ui::list(rows, selected_, scroll_, &colors, ui::BODY_Y + 20, 6);
            for (int i = 0; i < 6 && scroll_ + i < (int)lines_.size(); i++)
                if (!lines_[(size_t)(scroll_ + i)].iso.empty())
                    flags::drawSmall(lines_[(size_t)(scroll_ + i)].iso.c_str(), 6 + 29 * 6, ui::BODY_Y + 20 + i * ui::ROW_H + 1);
        }

        // Bottom line: what will be sent.
        std::string next = qso_.message();
        if (!status_.empty()) ui::text(6, ui::BODY_Y + 95, status_, ui::C_WARN);
        else if (!next.empty())
            ui::text(6, ui::BODY_Y + 95, std::string(txEnabled_ ? "TX " : "off ") + (txParity_ ? "odd: " : "even: ") + next,
                     txEnabled_ ? ui::C_NOTICE : ui::C_DIM);
        ui::footer(txEnabled_ ? "H halt   T pause   ENTER reply" : "ENTER reply  C CQ  T TX on  H halt");
    }

    // Simulator: select the newest decode that starts with `text`.
    bool selectForTest(const std::string& text) {
        for (int i = (int)lines_.size() - 1; i >= 0; i--)
            if (lines_[(size_t)i].text.compare(0, text.size(), text) == 0) {
                selected_ = i;
                follow_ = false;
                dirty = true;
                return true;
            }
        return false;
    }

private:
    struct Line {
        std::string text, country, iso;
        int snr = 0;
        float freq = 0;
        long long period = 0;
        bool cq = false, toMe = false, mine = false, newCountry = false;
        std::string from;
    };

    std::string dialMhz() const { return rig::connected() ? rig::freqMhz() : config::get("ft8_dial", "14.074"); }

    bool canTransmit() {
        if (qso_.myCall.empty()) { status_ = "Set your callsign in Settings first"; return false; }
        uint32_t dummy;
        if (!ft8::packCall(qso_.myCall, dummy)) { status_ = "Compound callsigns cannot transmit here"; return false; }
        if (qso_.myGrid.size() < 4) { status_ = "Set your grid in Settings first"; return false; }
        return true;
    }

    void startPeriod(int ms) {
        rx_ = false;
        bool ourTurn = txEnabled_ && !qso_.message().empty() && (int)(period_ % 2) == txParity_;
        if (ourTurn && ms < 2000) {
            startTransmit(ms);
            return;
        }
        if (ms > 1500) return;              // joined in the middle: wait for the next period
        if (!audio::capturing()) audio::startCapture(8000);
        decoder_->reset();
        resampler_ = ft8::Resampler();
        rx_ = true;
    }

    void startTransmit(int ms) {
        std::string text = qso_.message();
        uint8_t payload[10], tones[ft8::kSymbols];
        if (!ft8::pack(text, payload)) {
            status_ = "Cannot encode: " + text;
            txEnabled_ = false;
            return;
        }
        ft8::tones(payload, tones);
        float lead = 0.5f - ms / 1000.0f;
        synth_.reset(new ft8::Synth(tones, txHz_, 8000, 14000, lead > 0 ? lead : 0));
        audio::play([](int16_t* out, size_t max, void* self) { return ((Ft8App*)self)->synth_->read(out, max); }, this, 8000);
        tx_ = true;
        lastTxPeriod_ = period_;
        Line l;
        l.text = text;
        l.mine = true;
        l.period = period_;
        addLine(l);
        qso_.transmitted();
        if (qso_.step == ft8::Qso::DONE) {
            txEnabled_ = false;
            status_ = "Contact complete";
        }
        int limit = qso_.step == ft8::Qso::CQ ? 10 : 6;
        if (qso_.unanswered >= limit) {
            txEnabled_ = false;
            status_ = "No answer: TX off (T to go on)";
        }
    }

    void finishDecoding() {
        if (!decoding_ || !decoder_) return;
        while (decoder_->step(fresh_, 8, qso_.myCall)) {}
        decoding_ = false;
        for (const ft8::Decoded& d : fresh_) {
            Line l;
            l.text = d.text;
            l.snr = d.snr;
            l.freq = d.freqHz;
            l.period = period_;
            std::vector<std::string> w = ft8::splitWords(d.text);
            l.cq = !w.empty() && w[0] == "CQ";
            l.toMe = !w.empty() && w[0] == qso_.myCall;
            if (l.cq) {
                for (size_t i = 1; i < w.size(); i++)
                    if (w[i].size() > 2 && !ft8::isGrid4(w[i]) && w[i].find_first_of("0123456789") != std::string::npos) { l.from = w[i]; break; }
            } else if (w.size() >= 2) {
                l.from = w[1];
            }
            if (!l.from.empty()) {              // to PSK Reporter (when switched on)
                std::string loc = !w.empty() && ft8::isGrid4(w.back()) && w.back() != "RR73" ? w.back() : "";
                pskr::add(l.from, loc, "FT8", (uint32_t)(atof(dialMhz().c_str()) * 1e6 + d.freqHz), d.snr);
            }
            const dxcc::Entity* e = dxcc::lookup(l.from);
            if (e) {
                l.country = e->name;
                l.iso = e->iso;
                l.newCountry = !logbook::workedEntity(e);
            }
            addLine(l);
            bool logNow = false;
            if (qso_.received(d.text, d.snr, logNow)) {
                if (qso_.step != ft8::Qso::DONE) txEnabled_ = true;
                status_.clear();
                txParity_ = (int)((period_ + 1) % 2);   // answer in the other half of the minute
            }
            if (logNow) logContact();
            if (qso_.step == ft8::Qso::DONE) txEnabled_ = false;
        }
        fresh_.clear();
        dirty = true;
    }

    void answerSelected() {
        if (selected_ < 0 || selected_ >= (int)lines_.size()) return;
        const Line& l = lines_[(size_t)selected_];
        if (l.mine || l.from.empty() || l.from[0] == '<') return;
        if (!canTransmit()) return;
        if (l.toMe) {
            // Someone called us: carry on from their message.
            qso_.callCq();
            bool logNow = false;
            qso_.received(l.text, l.snr, logNow);
            if (logNow) logContact();
        } else {
            qso_.answer(l.from, l.snr);
            std::vector<std::string> w = ft8::splitWords(l.text);
            if (w.size() >= 3 && ft8::isGrid4(w.back()) && w.back() != "RR73") qso_.dxGrid = w.back();   // from "CQ DX1ABC PK04"
        }
        txParity_ = (int)((l.period + 1) % 2);          // the other half of the minute
        txEnabled_ = true;
        status_.clear();
    }

    void logContact() {
        adif::Qso q;
        q.call = qso_.dxCall;
        q.grid = qso_.dxGrid;
        q.mode = "FT8";
        q.freq = dialMhz();
        q.rstSent = ft8::Qso::report(qso_.reportSent);
        q.rstRcvd = ft8::Qso::report(qso_.reportRcvd);
        q.potaRef = config::get("pota_ref");
        q.sotaRef = config::get("sota_ref");
        time_t now = (time_t)(net::utcMillis() / 1000);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y%m%d", &tmv);
        q.date = buf;
        strftime(buf, sizeof(buf), "%H%M", &tmv);
        q.time = buf;
        logbook::add(q);
        status_ = "Logged " + q.call;
    }

    void addLine(const Line& l) {
        lines_.push_back(l);
        if (lines_.size() > 40) {
            lines_.erase(lines_.begin());
            if (selected_ > 0) selected_--;
        }
    }

    std::unique_ptr<ft8::Decoder> decoder_;
    std::unique_ptr<ft8::Synth> synth_;
    ft8::Resampler resampler_;
    ft8::Qso qso_;
    std::vector<ft8::Decoded> fresh_;
    std::vector<Line> lines_;
    std::string status_;
    long long period_ = -1, lastTxPeriod_ = -1;
    int txParity_ = 0, selected_ = 0, scroll_ = 0;
    float txHz_ = 1500;
    bool rx_ = false, tx_ = false, decoding_ = false, txEnabled_ = false, follow_ = true;
    uint32_t lastDrawMs_ = 0;
};
