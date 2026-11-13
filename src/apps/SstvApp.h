#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Audio.h"
#include "core/Config.h"
#include "core/Net.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "lib/Bmp.h"
#include "lib/Font5x7.h"
#include "lib/Sstv.h"

// SSTV pictures. RECEIVE: listens on the microphone, shows the picture as it
// arrives and saves it to /HamOS/sstv. SEND: pick a picture (24-bit BMP) from
// /HamOS/sstv, choose a mode, and it is played through the speaker /
// headphone jack with your callsign written on it.
class SstvApp : public App {
public:
    const char* name() const override { return "SSTV"; }
    uint16_t color() const override { return 0xFC9F; }

    void onEnter() override {
        size_t n;
        const sstv::Mode* all = sstv::modes(n);
        txMode_ = &all[0];
        std::string wanted = config::get("sstv_mode", "Martin M1");
        for (size_t i = 0; i < n; i++)
            if (wanted == all[i].name) txMode_ = &all[i];
        status_.clear();
        startReceive();
    }

    void onExit() override {
        audio::stopPlaying();
        audio::stopCapture();
        decoder_.reset();
        encoder_.reset();
    }

    bool onBack() override {
        if (encoder_) {                     // stop sending
            audio::stopPlaying();
            encoder_.reset();
            status_ = "Stopped";
            startReceive();
            page_ = SEND;
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (encoder_) return;
        if (k.tab) {
            page_ = page_ == RECEIVE ? SEND : RECEIVE;
            if (page_ == SEND) loadGallery();
            status_.clear();
        } else if (page_ == RECEIVE) {
            if (k.ch == 'm' || k.ch == 'M') {           // decode without a VIS header
                size_t n;
                const sstv::Mode* all = sstv::modes(n);
                forced_ = (forced_ + 1) % ((int)n + 1);
                decoder_->force(forced_ ? &all[forced_ - 1] : nullptr);
                status_ = forced_ ? std::string("Waiting for sync: ") + all[forced_ - 1].name : "Automatic (VIS)";
            } else if (k.ch == 'c' || k.ch == 'C') {
                clearPicture();
            } else {
                return;
            }
        } else {
            if (ui::listNav(k, selected_, (int)files_.size())) {
                previewDrawn_ = false;
            } else if (k.ch == 'm' || k.ch == 'M') {
                size_t n;
                const sstv::Mode* all = sstv::modes(n);
                size_t i = (size_t)(txMode_ - all);
                txMode_ = &all[(i + 1) % n];
                config::set("sstv_mode", txMode_->name);
                config::save();
            } else if (k.ch == 'o' || k.ch == 'O') {
                overlay_ = !overlay_;
            } else if (k.enter && !files_.empty()) {
                startSending();
            } else {
                return;
            }
        }
        dirty = true;
    }

    void onTick(uint32_t now) override {
        if (encoder_) {
            if (!audio::playing()) {
                encoder_.reset();
                status_ = "Sent";
                startReceive();
                dirty = true;
            } else if (now - lastDrawMs_ > 1000) {
                lastDrawMs_ = now;
                dirty = true;
            }
            return;
        }
        if (decoder_) {
            for (int i = 0; i < 12; i++) {          // catch up if the loop was slow
                const int16_t* block = audio::capture();
                if (!block) break;
                decoder_->feed(block, audio::BLOCK);
            }
        }
        if (now - lastDrawMs_ > 300) {              // tuning meter and progress
            lastDrawMs_ = now;
            if (page_ == RECEIVE) drawReceiveInfo();
        }
    }

    void draw() override {
        if (encoder_) drawSending();
        else if (page_ == RECEIVE) drawReceive();
        else drawGallery();
    }

    // Simulator checks
    const std::string& lastSavedForTest() const { return savedPath_; }
    bool selectForTest(const std::string& file) {
        for (size_t i = 0; i < files_.size(); i++)
            if (files_[i] == file) { selected_ = (int)i; return true; }
        return false;
    }

private:
    enum Page { RECEIVE, SEND };
    static constexpr int kRate = 11025;
    static constexpr int kPicW = 135, kPicH = 108;      // picture area on the screen
    static constexpr const char* kFolder = "/HamOS/sstv";

    // ---------------------------------------------------------- receive ----

    void startReceive() {
        if (!decoder_) {
            decoder_.reset(new sstv::Decoder(kRate));
            decoder_->onLine(onLine, this);
        }
        audio::startCapture(kRate);
    }

    void clearPicture() {
        ui::fill(0, ui::BODY_Y, kPicW, kPicH, ui::C_BG);
        lastRow_ = -1;
        savedPath_.clear();
    }

    static void onLine(void* self, const sstv::Mode& m, int y, const uint8_t* rgb) { ((SstvApp*)self)->line(m, y, rgb); }

    void line(const sstv::Mode& m, int y, const uint8_t* rgb) {
        if (y == 0) {                               // a new picture: new file
            char name[48];
            if (net::timeValid()) {
                time_t now = time(nullptr);
                struct tm t;
                gmtime_r(&now, &t);
                strftime(name, sizeof(name), "/rx%y%m%d-%H%M%S.bmp", &t);
            } else {
                snprintf(name, sizeof(name), "/rx%lu.bmp", (unsigned long)millis());
            }
            path_ = std::string(kFolder) + name;
            uint8_t head[54];
            bmp::header(head, m.width, m.height);
            storage::writeFile(path_.c_str(), std::string((const char*)head, 54));
            if (page_ == RECEIVE) ui::fill(0, ui::BODY_Y, kPicW, kPicH, ui::C_BG);
            lastRow_ = -1;
        }
        std::string row((size_t)m.width * 3 + 4, '\0');
        row.resize(bmp::packRow(rgb, m.width, (uint8_t*)&row[0]));
        storage::appendFile(path_.c_str(), row);
        if (y == m.height - 1) {
            savedPath_ = path_;
            status_ = "Saved " + path_.substr(path_.rfind('/') + 1);
        }
        // Scaled down onto the screen: each screen row once, from the source rows that map to it.
        int row0 = y * kPicH / m.height;
        if (page_ != RECEIVE || row0 == lastRow_) return;
        lastRow_ = row0;
        uint16_t px[kPicW];
        for (int x = 0; x < kPicW; x++) {
            int sx0 = x * m.width / kPicW, sx1 = std::max(sx0 + 1, (x + 1) * m.width / kPicW);
            int r = 0, g = 0, b = 0;
            for (int sx = sx0; sx < sx1; sx++) { r += rgb[sx * 3]; g += rgb[sx * 3 + 1]; b += rgb[sx * 3 + 2]; }
            int n = sx1 - sx0;
            px[x] = be565(r / n, g / n, b / n);
        }
        ui::image(0, ui::BODY_Y + row0, kPicW, 1, px);
    }

    static uint16_t be565(int r, int g, int b) {
        uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        return (uint16_t)((v >> 8) | (v << 8));     // high byte first in memory
    }

    void drawReceive() {
        ui::clearBody();
        ui::frame(0, ui::BODY_Y, kPicW, kPicH, ui::C_FOOT);
        if (!savedPath_.empty()) showFile(savedPath_, 0, ui::BODY_Y, kPicW, kPicH);
        drawReceiveInfo();
        ui::footer(forced_ ? "M mode (manual)  C clear  TAB send" : "M manual mode  C clear  TAB send");
    }

    void drawReceiveInfo() {
        int x = kPicW + 6;
        ui::fill(x - 2, ui::BODY_Y, ui::W - x + 2, kPicH, ui::C_BG);
        const sstv::Mode* m = decoder_ ? decoder_->mode() : nullptr;
        bool busy = decoder_ && decoder_->receiving();
        ui::text(x, ui::BODY_Y + 2, "RECEIVE", ui::C_SEL);
        ui::text(x, ui::BODY_Y + 14, busy && m ? m->name : "listening", busy ? ui::C_OK : ui::C_DIM);
        if (busy && m) {
            char buf[24];
            snprintf(buf, sizeof(buf), "line %d/%d", decoder_->lines(), m->height);
            ui::text(x, ui::BODY_Y + 26, buf, ui::C_TEXT);
            int w = ui::W - x - 6;
            ui::fill(x, ui::BODY_Y + 38, w, 3, ui::C_FOOT);
            ui::fill(x, ui::BODY_Y + 38, w * decoder_->lines() / m->height, 3, ui::C_OK);
        }
        // Tuning meter: 1100 .. 2300 Hz; sync 1200, black 1500, white 2300.
        int top = ui::BODY_Y + 48, h = 52, mx = x + 4;
        ui::frame(mx, top, 8, h, ui::C_DIM);
        for (int f : {1200, 1500, 1900, 2300}) {
            int yy = top + h - 1 - (f - 1100) * (h - 1) / 1200;
            ui::line(mx + 9, yy, mx + 12, yy, ui::C_DIM);
        }
        ui::text(mx + 15, top - 2, "2300", ui::C_DIM);
        ui::text(mx + 15, top + h - 8, "1100", ui::C_DIM);
        float f = decoder_ ? decoder_->frequency() : 1100;
        f = std::max(1100.0f, std::min(2300.0f, f));
        int fy = top + h - 1 - (int)((f - 1100) * (h - 1) / 1200);
        ui::fill(mx + 1, fy - 1, 6, 3, ui::C_NOTICE);
        if (!status_.empty()) {
            std::string s = status_.size() > 16 ? status_.substr(0, 16) : status_;
            ui::text(x, ui::BODY_Y + 100 - 2, s, ui::C_WARN);
        }
    }

    // ---------------------------------------------------------- gallery ----

    void loadGallery() {
        files_.clear();
        for (const std::string& f : storage::listFiles(kFolder)) {
            std::string lower = f;
            for (char& c : lower) c = (char)tolower((unsigned char)c);
            if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".bmp") files_.push_back(f);
        }
        if (selected_ >= (int)files_.size()) selected_ = 0;
        previewDrawn_ = false;
    }

    void drawGallery() {
        ui::clearBody();
        if (files_.empty()) {
            ui::textCentered(ui::BODY_Y + 30, "No pictures", ui::C_WARN, 1.5);
            ui::textCentered(ui::BODY_Y + 52, "Put 24-bit BMP files in /HamOS/sstv", ui::C_DIM);
            ui::footer("TAB receive   ` back");
            return;
        }
        // File list and settings on the left, preview on the right.
        int rows = 6, top = ui::BODY_Y, listW = 112;
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
        for (int r = 0; r < rows && scroll_ + r < (int)files_.size(); r++) {
            int i = scroll_ + r;
            uint16_t bg = i == selected_ ? ui::C_SEL : ui::C_BG;
            if (i == selected_) ui::fill(0, top + r * ui::ROW_H, listW, ui::ROW_H, bg);
            std::string name = files_[(size_t)i];
            name = name.substr(0, name.size() - 4).substr(0, 18);         // without ".bmp"
            ui::textOn(3, top + r * ui::ROW_H + 2, name, ui::C_TEXT, bg);
        }
        char line[48];
        snprintf(line, sizeof(line), "%s %.0fs", txMode_->name, sstv::transmissionMs(*txMode_, true) / 1000);
        ui::text(3, top + rows * ui::ROW_H + 6, line, ui::C_OK);
        ui::text(3, top + rows * ui::ROW_H + 20, overlay_ ? "with callsign" : "no callsign", ui::C_DIM);
        int px = listW + 4, pw = ui::W - px, ph = pw * 256 / 320;
        ui::frame(px, top, pw, ph, ui::C_FOOT);
        showFile(std::string(kFolder) + "/" + files_[(size_t)selected_], px, top, pw, ph);
        ui::footer("ENTER send  M mode  O call  TAB rx");
        previewDrawn_ = true;
    }

    // Draws a BMP file scaled into a box (nearest pixel).
    void showFile(const std::string& path, int x0, int y0, int w, int h) {
        uint8_t head[54];
        bmp::Info info;
        if (storage::readAt(path.c_str(), 0, head, 54) != 54 || !bmp::parse(head, 54, info)) {
            ui::text(x0 + 4, y0 + h / 2 - 4, "not a 24-bit BMP", ui::C_WARN);
            return;
        }
        std::vector<uint8_t> row(info.rowBytes);
        std::vector<uint16_t> px((size_t)w);
        for (int y = 0; y < h; y++) {
            int sy = y * info.height / h;
            if (storage::readAt(path.c_str(), bmp::rowOffset(info, sy), row.data(), info.rowBytes) != info.rowBytes) break;
            for (int x = 0; x < w; x++) {
                const uint8_t* p = &row[(size_t)(x * info.width / w) * info.bytesPerPixel];
                px[(size_t)x] = be565(p[2], p[1], p[0]);
            }
            ui::image(x0, y0 + y, w, 1, px.data());
        }
    }

    // ---------------------------------------------------------- sending ----

    void startSending() {
        std::string path = std::string(kFolder) + "/" + files_[(size_t)selected_];
        uint8_t head[54];
        if (storage::readAt(path.c_str(), 0, head, 54) != 54 || !bmp::parse(head, 54, txInfo_)) {
            status_ = "Not a 24-bit BMP";
            return;
        }
        txPath_ = path;
        txRow_.resize(txInfo_.rowBytes);
        callsign_ = overlay_ ? config::get("callsign") : "";
        audio::stopCapture();
        encoder_.reset(new sstv::Encoder(*txMode_, source, this, kRate));
        audio::play([](int16_t* out, size_t max, void* self) { return ((SstvApp*)self)->encoder_->read(out, max); }, this, kRate);
        status_.clear();
    }

    // One line of the picture being sent: the file scaled to the mode, with the callsign on top.
    static bool source(void* self, int y, uint8_t* rgb) { return ((SstvApp*)self)->txLine(y, rgb); }

    bool txLine(int y, uint8_t* rgb) {
        const sstv::Mode& m = *txMode_;
        int sy = y * txInfo_.height / m.height;
        if (storage::readAt(txPath_.c_str(), bmp::rowOffset(txInfo_, sy), txRow_.data(), txInfo_.rowBytes) != txInfo_.rowBytes)
            return false;
        for (int x = 0; x < m.width; x++) {
            const uint8_t* p = &txRow_[(size_t)(x * txInfo_.width / m.width) * txInfo_.bytesPerPixel];
            rgb[x * 3] = p[2];
            rgb[x * 3 + 1] = p[1];
            rgb[x * 3 + 2] = p[0];
        }
        // Callsign in the top left corner, white with a black outline, 3 pixels per font pixel
        // (twice that on the wide PD modes).
        int scale = m.width >= 640 ? 6 : 3, x0 = 4 * scale / 3, y0 = 4 * scale / 3;
        if (!callsign_.empty() && y >= y0 - 1 && y < y0 + 8 * scale) {
            for (int x = 0; x < m.width; x++) {
                bool on = false, near = false;
                for (int dy = -1; dy <= 1 && !on; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int px = x + dx - x0, py = y + dy - y0;
                        if (px < 0 || py < 0) continue;
                        int c = px / scale / 6, gx = px / scale % 6, gy = py / scale;
                        if (c >= (int)callsign_.size() || gx >= 5 || gy >= 7) continue;
                        const uint8_t* g = font5x7::glyph(callsign_[(size_t)c]);
                        if (g && (g[gx] >> gy & 1)) {
                            if (dx == 0 && dy == 0) on = true;
                            else near = true;
                        }
                    }
                if (on) rgb[x * 3] = rgb[x * 3 + 1] = rgb[x * 3 + 2] = 255;
                else if (near) rgb[x * 3] = rgb[x * 3 + 1] = rgb[x * 3 + 2] = 0;
            }
        }
        return true;
    }

    void drawSending() {
        ui::clearBody();
        ui::text(6, ui::BODY_Y + 6, std::string("SENDING  ") + txMode_->name, ui::C_ERR, 1.5);
        ui::text(6, ui::BODY_Y + 26, txPath_.substr(txPath_.rfind('/') + 1), ui::C_TEXT);
        float p = encoder_ ? encoder_->progress() : 1;
        ui::fill(6, ui::BODY_Y + 44, ui::W - 12, 8, ui::C_FOOT);
        ui::fill(6, ui::BODY_Y + 44, (int)((ui::W - 12) * p), 8, ui::C_ERR);
        char buf[48];
        double total = sstv::transmissionMs(*txMode_, true) / 1000;
        snprintf(buf, sizeof(buf), "line %d of %d   %.0f s left", encoder_ ? encoder_->currentLine() : 0, txMode_->height,
                 total * (1 - p));
        ui::text(6, ui::BODY_Y + 58, buf, ui::C_DIM);
        ui::text(6, ui::BODY_Y + 76, "Headphone jack to the radio, or VOX", ui::C_DIM);
        ui::footer("` stop sending");
    }

    std::unique_ptr<sstv::Decoder> decoder_;
    std::unique_ptr<sstv::Encoder> encoder_;
    const sstv::Mode* txMode_ = nullptr;
    bmp::Info txInfo_;
    std::string txPath_, callsign_, path_, savedPath_, status_;
    std::vector<uint8_t> txRow_;
    std::vector<std::string> files_;
    Page page_ = RECEIVE;
    int selected_ = 0, scroll_ = 0, forced_ = 0, lastRow_ = -1;
    bool overlay_ = true, previewDrawn_ = false;
    uint32_t lastDrawMs_ = 0;
};
