#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Ui.h"
#include "lib/Psk31.h"

// PSK31 keyboard chat: a waterfall to find signals, the received text, and a
// line to type what to send. Hold the Cardputer to the receiver's speaker;
// send through the speaker / headphone jack.
class PskApp : public App {
public:
    const char* name() const override { return "PSK31"; }
    uint16_t color() const override { return 0x5FEB; }

    void onEnter() override {
        double hz = atof(config::get("psk_hz", "1000").c_str());
        if (hz < kMinHz + 50 || hz > kMaxHz - 50) hz = 1000;
        demod_.reset(new psk31::Demodulator(kRate));
        demod_->setFrequency(hz);
        spectrum_.reset(new psk31::Spectrum(kRate));
        rows_.assign((size_t)kWfRows * ui::W, 0);
        input_.maxLen = 120;
        audio::startCapture(kRate);
    }

    void onExit() override {
        config::set("psk_hz", std::to_string((int)std::lround(demod_ ? demod_->frequency() : 1000)));
        config::save();
        audio::stopPlaying();
        audio::stopCapture();
        demod_.reset();
        spectrum_.reset();
        tx_.reset();
    }

    bool onBack() override {
        if (tx_) {
            audio::stopPlaying();
            tx_.reset();
            audio::startCapture(kRate);
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (k.fnLeft() || k.fnRight()) {
            tune(demod_->frequency() + (k.fnRight() ? 10 : -10));
        } else if (k.fnUp() || k.fnDown()) {
            tune(demod_->frequency() + (k.fnUp() ? 1 : -1));
        } else if (k.tab) {
            snap();
        } else if (k.enter && !tx_) {
            std::string text = input_.value;
            if (text.empty()) {
                std::string call = config::get("callsign");
                if (call.empty()) { addLine("Set your callsign in Settings first", ui::C_WARN); dirty = true; return; }
                text = "CQ CQ CQ de " + call + " " + call + " " + call + " pse k";
            }
            send(text);
            input_.value.clear();
        } else if (!k.fn && input_.handle(k)) {
        } else {
            return;
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (tx_) {
            if (!audio::playing()) {
                tx_.reset();
                audio::startCapture(kRate);
                dirty = true;
            }
            return;
        }
        for (int i = 0; i < 8; i++) {
            const int16_t* block = audio::capture();
            if (!block) break;
            demod_->feed(block, audio::BLOCK);
            for (size_t j = 0; j < audio::BLOCK; j++)
                if (spectrum_->feed(block[j], 512)) addSpectrumRow();
        }
        if (!demod_->text.empty()) {
            for (char c : demod_->text) {
                if (c == '\n' || lines_.empty() || lines_.back().tx) addLine("", ui::C_TEXT);
                if (c != '\n') lines_.back().text += c;
            }
            demod_->text.clear();
            textDirty_ = true;
        }
        if (now - lastDrawMs_ >= 250) {
            lastDrawMs_ = now;
            drawWaterfall();
            if (textDirty_) drawText();
            textDirty_ = false;
        }
    }

    void draw() override {
        ui::clearBody();
        drawWaterfall();
        drawText();
        ui::footer(tx_ ? T("` stop sending") : T("Fn+arrows tune  TAB snap  ENTER send"));
    }

    // Simulator
    std::string receivedForTest() const {
        std::string all;
        for (const Line& l : lines_)
            if (!l.tx) all += l.text + "\n";
        return all;
    }
    double frequencyForTest() const { return demod_ ? demod_->frequency() : 0; }

private:
    static constexpr int kRate = 8000, kWfRows = 34, kTextRows = 5;
    static constexpr double kMinHz = 300, kMaxHz = 2700;
    struct Line {
        std::string text;
        uint16_t color;
        bool tx;
    };

    double hzPerPixel() const { return (kMaxHz - kMinHz) / ui::W; }

    void tune(double hz) {
        demod_->setFrequency(std::max(kMinHz + 30, std::min(kMaxHz - 30, hz)));
        drawWaterfall();
    }

    // Moves to the strongest signal within 150 Hz of the cursor.
    void snap() {
        double hz = demod_->frequency(), per = spectrum_->hzPerBin();
        int lo = (int)((hz - 150) / per), hi = (int)((hz + 150) / per), best = -1;
        for (int b = std::max(lo, 1); b <= std::min(hi, psk31::Spectrum::N / 2 - 2); b++)
            if (best < 0 || spectrum_->db[b] > spectrum_->db[best]) best = b;
        if (best < 0) return;
        // A PSK31 signal has its two strongest lines 15.6 Hz either side of the
        // carrier: take the centre of gravity of the power within 25 Hz of the peak.
        double sum = 0, moment = 0;
        int span = (int)std::ceil(25 / per);
        for (int b = std::max(best - span, 1); b <= std::min(best + span, psk31::Spectrum::N / 2 - 1); b++) {
            double w = std::pow(10.0, spectrum_->db[b] / 10);
            sum += w;
            moment += w * b;
        }
        tune(sum > 0 ? moment / sum * per : best * per);
    }

    void addSpectrumRow() {
        // Noise floor from the median of the row: colours span floor .. floor + 36 dB.
        std::vector<float> row((size_t)ui::W);
        double per = spectrum_->hzPerBin();
        for (int x = 0; x < ui::W; x++) {
            int b0 = (int)((kMinHz + x * hzPerPixel()) / per), b1 = std::max(b0 + 1, (int)((kMinHz + (x + 1) * hzPerPixel()) / per));
            float m = -200;
            for (int b = b0; b < b1 && b < psk31::Spectrum::N / 2; b++) m = std::max(m, spectrum_->db[b]);
            row[(size_t)x] = m;
        }
        std::vector<float> sorted = row;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        float floor = sorted[sorted.size() / 2];
        floor_ = floor_ < -150 ? floor : 0.9f * floor_ + 0.1f * floor;
        std::rotate(rows_.rbegin(), rows_.rbegin() + ui::W, rows_.rend());      // newest row on top
        for (int x = 0; x < ui::W; x++) rows_[(size_t)x] = heat((row[(size_t)x] - floor_) / 36.0f);
    }

    // Black - blue - cyan - yellow - white, stored high byte first for ui::image.
    static uint16_t heat(float v) {
        v = std::max(0.0f, std::min(1.0f, v));
        int r, g, b;
        if (v < 0.25f) { r = 0; g = 0; b = (int)(v * 4 * 200); }
        else if (v < 0.5f) { r = 0; g = (int)((v - 0.25f) * 4 * 255); b = 200; }
        else if (v < 0.75f) { r = (int)((v - 0.5f) * 4 * 255); g = 255; b = (int)(200 * (1 - (v - 0.5f) * 4)); }
        else { r = 255; g = 255; b = (int)((v - 0.75f) * 4 * 255); }
        uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        return (uint16_t)((c >> 8) | (c << 8));
    }

    void drawWaterfall() {
        int top = ui::BODY_Y;
        for (int y = 0; y < kWfRows; y++) ui::image(0, top + y, ui::W, 1, &rows_[(size_t)y * ui::W]);
        // Cursor: the tuned frequency and the width of a PSK31 signal.
        int x = (int)((demod_->frequency() - kMinHz) / hzPerPixel());
        int half = std::max(2, (int)(31.25 / hzPerPixel()));
        ui::line(x - half, top, x - half, top + 3, ui::C_ERR);
        ui::line(x + half, top, x + half, top + 3, ui::C_ERR);
        ui::line(x, top + kWfRows - 6, x, top + kWfRows - 1, ui::C_ERR);
        // Frequency and signal quality on a strip under the waterfall.
        int y = top + kWfRows;
        ui::fill(0, y, ui::W, 10, ui::C_FOOT);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s %.1f Hz", tx_ ? "TX" : "RX", demod_->frequency());
        ui::textOn(3, y + 1, buf, tx_ ? ui::C_ERR : ui::C_TEXT, ui::C_FOOT);
        float q = std::max(0.0f, std::min(1.0f, (demod_->quality - 0.5f) * 2));
        ui::fill(150, y + 2, 86, 6, ui::C_BG);
        ui::fill(150, y + 2, (int)(86 * q), 6, q > 0.24f ? ui::C_OK : ui::C_WARN);
    }

    void addLine(const std::string& text, uint16_t color, bool tx = false) {
        lines_.push_back({text, color, tx});
        if (lines_.size() > 40) lines_.erase(lines_.begin());
    }

    void drawText() {
        int top = ui::BODY_Y + kWfRows + 11;
        // Wrap everything into screen lines and show the newest ones.
        std::vector<Line> shown;
        for (const Line& l : lines_)
            for (const std::string& part : ui::wrapLines(l.text.empty() ? " " : l.text, 39)) shown.push_back({part, l.color, l.tx});
        ui::fill(0, top, ui::W, kTextRows * 10 + 2, ui::C_BG);
        int first = std::max(0, (int)shown.size() - kTextRows);
        for (int i = first; i < (int)shown.size(); i++)
            ui::text(2, top + (i - first) * 10, shown[(size_t)i].text, shown[(size_t)i].color);
        // Input line
        int y = ui::BODY_Y + ui::BODY_H - 11;
        ui::fill(0, y, ui::W, 11, ui::C_FOOT);
        std::string in = input_.value.size() > 36 ? input_.value.substr(input_.value.size() - 36) : input_.value;
        ui::textOn(2, y + 2, "> " + in + "_", ui::C_TEXT, ui::C_FOOT);
    }

    void send(const std::string& text) {
        addLine(text, ui::C_NOTICE, true);
        tx_.reset(new psk31::Modulator(text + " ", demod_->frequency(), kRate, 12000));
        audio::play([](int16_t* out, size_t max, void* self) { return ((PskApp*)self)->tx_->read(out, max); }, this, kRate);
        audio::markTransmit();
    }

    std::unique_ptr<psk31::Demodulator> demod_;
    std::unique_ptr<psk31::Spectrum> spectrum_;
    std::unique_ptr<psk31::Modulator> tx_;
    std::vector<uint16_t> rows_;
    std::vector<Line> lines_;
    ui::TextInput input_;
    float floor_ = -200;
    bool textDirty_ = true;
    uint32_t lastDrawMs_ = 0;
};
