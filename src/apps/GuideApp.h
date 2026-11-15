#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Storage.h"
#include "core/Ui.h"

// Reading pages from /HamOS/guide on the SD card (UTF-8 text files, Turkish
// letters are shown). Simple markup:
//   # Title          (first line: the name in the list)
//   ## Heading
//   - bullet
//   > note
// Any other line is a paragraph; an empty line separates paragraphs.
class GuideApp : public App {
public:
    const char* name() const override { return T("Guide"); }
    uint16_t color() const override { return 0x2E8B; }

    void onEnter() override {
        loadIndex();
        open_ = false;
    }

    bool onBack() override {
        if (!open_) return false;
        open_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (!open_) {
            if (k.enter && !files_.empty()) {
                openArticle(files_[(size_t)selected_]);
                open_ = true;
            } else if (!ui::listNav(k, selected_, (int)files_.size())) {
                return;
            }
        } else {
            int maxTop = (int)lines_.size() > kRows ? (int)lines_.size() - kRows : 0;
            int before = top_;
            if (k.down()) top_++;
            else if (k.up()) top_--;
            else if (k.right() || k.ch == ' ') top_ += kRows - 1;      // page down
            else if (k.left()) top_ -= kRows - 1;
            else return;
            if (top_ > maxTop) top_ = maxTop;
            if (top_ < 0) top_ = 0;
            if (top_ == before) return;
        }
        dirty = true;
    }

    void draw() override {
        if (!open_) {
            if (files_.empty()) {
                ui::clearBody();
                ui::textCentered(ui::BODY_Y + 24, T("No guide pages"), ui::C_WARN, 1.5);
                ui::textCentered(ui::BODY_Y + 50, T("Copy the sdcard folder from the"), ui::C_DIM);
                ui::textCentered(ui::BODY_Y + 62, T("HamOS repository to the SD card"), ui::C_DIM);
                ui::footer(T("` back"));
                return;
            }
            ui::list(titles_, selected_, scroll_);
            ui::footer(T("ENTER read   ` back"));
            return;
        }
        ui::clearBody();
        for (int r = 0; r < kRows && top_ + r < (int)lines_.size(); r++) {
            const Line& l = lines_[(size_t)(top_ + r)];
            ui::text(6, ui::BODY_Y + 2 + r * 12, l.text, l.color);
        }
        // Scroll bar
        if ((int)lines_.size() > kRows) {
            int h = ui::BODY_H * kRows / (int)lines_.size();
            int y = ui::BODY_Y + (ui::BODY_H - h) * top_ / ((int)lines_.size() - kRows);
            ui::fill(ui::W - 3, ui::BODY_Y, 3, ui::BODY_H, ui::C_FOOT);
            ui::fill(ui::W - 3, y, 3, h < 4 ? 4 : h, ui::C_DIM);
        }
        char hint[48];
        int pct = lines_.size() <= (size_t)kRows ? 100 : 100 * (top_ + kRows) / (int)lines_.size();
        snprintf(hint, sizeof(hint), "; . line   , / page   %d%%", pct > 100 ? 100 : pct);
        ui::footer(hint);
    }

private:
    static constexpr int kRows = 9;
    static constexpr size_t kWidth = 37;
    static constexpr const char* kFolder = "/HamOS/guide";
    struct Line {
        std::string text;
        uint16_t color;
    };

    void loadIndex() {
        files_.clear();
        titles_.clear();
        for (const std::string& f : storage::listFiles(kFolder)) {
            if (f.size() < 5 || f.substr(f.size() - 4) != ".txt") continue;
            char head[160] = {0};
            storage::readAt((std::string(kFolder) + "/" + f).c_str(), 0, head, sizeof(head) - 1);
            std::string first(head);
            first = first.substr(0, first.find('\n'));
            if (!first.empty() && first.back() == '\r') first.pop_back();
            if (first.size() >= 3 && (unsigned char)first[0] == 0xEF) first.erase(0, 3);    // UTF-8 byte order mark
            std::string title = first.compare(0, 2, "# ") == 0 ? first.substr(2) : f.substr(0, f.size() - 4);
            files_.push_back(f);
            titles_.push_back(ui::displayable(title));
        }
        if (selected_ >= (int)files_.size()) selected_ = 0;
    }

    void add(const std::string& text, uint16_t color, const std::string& indent = "") {
        bool first = true;
        for (const std::string& part : ui::wrapLines(text, kWidth - indent.size())) {
            lines_.push_back({(first ? "" : indent) + part, color});
            first = false;
        }
    }

    void openArticle(const std::string& file) {
        lines_.clear();
        top_ = 0;
        std::string body;
        storage::readFile((std::string(kFolder) + "/" + file).c_str(), body);
        if (body.size() >= 3 && (unsigned char)body[0] == 0xEF) body.erase(0, 3);
        body = ui::displayable(body);
        std::string paragraph;
        auto flush = [&]() {
            if (!paragraph.empty()) add(paragraph, ui::C_TEXT);
            paragraph.clear();
        };
        size_t pos = 0;
        while (pos <= body.size()) {
            size_t end = body.find('\n', pos);
            if (end == std::string::npos) end = body.size();
            std::string line = body.substr(pos, end - pos);
            pos = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 2, "# ") == 0) {
                flush();
                add(line.substr(2), ui::C_SEL);
                lines_.push_back({"", ui::C_TEXT});
            } else if (line.compare(0, 3, "## ") == 0) {
                flush();
                if (!lines_.empty() && !lines_.back().text.empty()) lines_.push_back({"", ui::C_TEXT});
                add(line.substr(3), ui::C_OK);
            } else if (line.compare(0, 2, "- ") == 0) {
                flush();
                add("- " + line.substr(2), ui::C_TEXT, "  ");
            } else if (line.compare(0, 2, "> ") == 0) {
                flush();
                add(line.substr(2), ui::C_WARN);
            } else if (line.empty()) {
                flush();
                if (!lines_.empty() && !lines_.back().text.empty()) lines_.push_back({"", ui::C_TEXT});
            } else {
                paragraph += (paragraph.empty() ? "" : " ") + line;
            }
            if (pos > body.size()) break;
        }
        flush();
        while (!lines_.empty() && lines_.back().text.empty()) lines_.pop_back();
    }

    std::vector<std::string> files_, titles_;
    std::vector<Line> lines_;
    bool open_ = false;
    int selected_ = 0, scroll_ = 0, top_ = 0;
};
