#pragma once
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Ui.h"

// Home screen: a grid of app tiles. main.cpp reads `launch` after a key.
class Launcher : public App {
public:
    explicit Launcher(const std::vector<App*>& apps) : apps_(apps) {}
    const char* name() const override { return T("HamOS"); }

    void onKey(const KeyEvent& k) override {
        int n = (int)apps_.size(), before = selected_;
        if (n == 0) return;
        if (k.left() && selected_ > 0) selected_--;
        if (k.right() && selected_ < n - 1) selected_++;
        if (k.up() && selected_ >= COLS) selected_ -= COLS;
        if (k.down() && selected_ + COLS < n) selected_ += COLS;
        if (selected_ != before) dirty = true;
        if (k.enter) launch = apps_[selected_];
    }

    void draw() override {
        int row = selected_ / COLS;
        if (row < scrollRow_) scrollRow_ = row;
        if (row >= scrollRow_ + ROWS) scrollRow_ = row - ROWS + 1;
        ui::clearBody();
        for (int i = scrollRow_ * COLS; i < (int)apps_.size() && i < (scrollRow_ + ROWS) * COLS; i++) {
            int x = 4 + (i % COLS) * (TILE_W + 4);
            int y = ui::BODY_Y + 3 + (i / COLS - scrollRow_) * (TILE_H + 3);
            bool sel = (i == selected_);
            ui::fill(x, y, TILE_W, TILE_H, sel ? ui::C_TEXT : ui::C_FOOT);
            ui::fill(x + 1, y + 1, TILE_W - 2, TILE_H - 2, sel ? 0x2124 : ui::C_FOOT);
            ui::fill(x + 1, y + 1, 5, TILE_H - 2, apps_[i]->color());
            ui::tileLabel(x + 12, y + TILE_H / 2, apps_[i]->name(), sel ? 0x2124 : ui::C_FOOT);
        }
        int rows = ((int)apps_.size() + COLS - 1) / COLS;
        bool above = scrollRow_ > 0, below = scrollRow_ + ROWS < rows;
        ui::footer(above && below ? T("Arrows select  ENTER open  more ^v")
                   : below ? T("Arrows select  ENTER open  more v")
                   : above ? T("Arrows select  ENTER open  more ^") : T("Arrows select   ENTER open"));
    }

    App* launch = nullptr;

private:
    static constexpr int COLS = 2, ROWS = 3, TILE_W = 114, TILE_H = 31;
    const std::vector<App*>& apps_;
    int selected_ = 0, scrollRow_ = 0;
};
