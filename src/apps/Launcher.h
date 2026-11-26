#pragma once
#include <cctype>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Ui.h"
#include "lib/Emergency.h"

// Home screen: a grid of app tiles.
//   TAB        next group (All, Favourites, Log, Digital, Mesh/APRS, Radio, Tools, Info)
//   letters    filter by name ("ft" -> FT8, ...); DEL removes a letter, ` clears
//   Fn+ENTER   adds / removes the selected app from Favourites
// main.cpp reads `launch` after a key.
class Launcher : public App {
public:
    enum Group { ALL, FAVOURITES, LOG, DIGITAL, MESH, RADIO, TOOLS, INFO, GROUPS };

    Launcher(const std::vector<App*>& apps, const std::vector<int>& groups) : apps_(apps), groups_(groups) {}
    const char* name() const override { return T("HamOS"); }

    void onEnter() override { rebuild(); }

    bool onBack() override {
        if (!filter_.empty() || group_ != ALL) {
            filter_.clear();
            group_ = ALL;
            rebuild();
            dirty = true;
            return true;
        }
        return false;
    }

    void onKey(const KeyEvent& k) override {
        if (visible_.empty() && !k.tab && !k.del && !isalpha((unsigned char)k.ch)) return;
        int n = (int)visible_.size(), before = selected_;
        if (k.tab) {
            group_ = (Group)((group_ + 1) % GROUPS);
            filter_.clear();
            rebuild();
            dirty = true;
            return;
        }
        if (k.enter && k.fn && n) {
            toggleFavourite(visible_[selected_]);
            if (group_ == FAVOURITES) rebuild();
            dirty = true;
            return;
        }
        if (k.del) {
            if (!filter_.empty()) {
                filter_.pop_back();
                rebuild();
                dirty = true;
            }
            return;
        }
        if (!k.fn && isalpha((unsigned char)k.ch)) {
            filter_ += (char)tolower((unsigned char)k.ch);
            rebuild();
            dirty = true;
            return;
        }
        if (k.left() && selected_ > 0) selected_--;
        if (k.right() && selected_ < n - 1) selected_++;
        if (k.up() && selected_ >= COLS) selected_ -= COLS;
        if (k.down() && selected_ + COLS < n) selected_ += COLS;
        if (selected_ != before) dirty = true;
        if (k.enter && n) launch = apps_[visible_[selected_]];
    }

    void draw() override {
        int row = selected_ / COLS;
        if (row < scrollRow_) scrollRow_ = row;
        if (row >= scrollRow_ + ROWS) scrollRow_ = row - ROWS + 1;
        ui::clearBody();
        static const char* names[GROUPS] = {"All", "Favourites", "Log", "Digital", "Mesh/APRS", "Radio", "Tools", "Info"};
        std::string head = std::string(T(names[group_])) + "  " + std::to_string(group_ + 1) + "/" + std::to_string(GROUPS);
        ui::text(4, ui::BODY_Y + 1, head, ui::C_NOTICE);
        if (!filter_.empty()) ui::text(130, ui::BODY_Y + 1, std::string("> ") + filter_ + "_", ui::C_OK);
        int top = ui::BODY_Y + HEAD_H;
        if (visible_.empty()) {
            ui::textCentered(top + 30, group_ == FAVOURITES && filter_.empty() ? T("Fn+ENTER on an app adds it here") : T("No app matches"),
                             ui::C_DIM);
        }
        for (int v = scrollRow_ * COLS; v < (int)visible_.size() && v < (scrollRow_ + ROWS) * COLS; v++) {
            int i = visible_[v];
            int x = 4 + (v % COLS) * (TILE_W + 4);
            int y = top + (v / COLS - scrollRow_) * (TILE_H + 3);
            bool sel = (v == selected_);
            ui::fill(x, y, TILE_W, TILE_H, sel ? ui::C_TEXT : ui::C_FOOT);
            ui::fill(x + 1, y + 1, TILE_W - 2, TILE_H - 2, sel ? 0x2124 : ui::C_FOOT);
            ui::fill(x + 1, y + 1, 5, TILE_H - 2, apps_[i]->color());
            ui::tileLabel(x + 12, y + TILE_H / 2, apps_[i]->name(), sel ? 0x2124 : ui::C_FOOT);
            if (isFavourite(i)) ui::fill(x + TILE_W - 6, y + 3, 3, 3, ui::C_NOTICE);
        }
        int rows = ((int)visible_.size() + COLS - 1) / COLS;
        bool above = scrollRow_ > 0, below = scrollRow_ + ROWS < rows;
        ui::footer(above && below ? T("Arrows select  ENTER open  more ^v")
                   : below ? T("Arrows select  ENTER open  more v")
                   : above ? T("Arrows select  ENTER open  more ^") : T("TAB group  type to search  Fn+ENTER fav"));
    }

    App* launch = nullptr;

private:
    static constexpr int COLS = 2, ROWS = 3, TILE_W = 114, TILE_H = 28, HEAD_H = 12;
    const std::vector<App*>& apps_;
    const std::vector<int>& groups_;
    std::vector<int> visible_;
    Group group_ = ALL;
    std::string filter_;
    int selected_ = 0, scrollRow_ = 0;

    std::string favourites() const { return "," + config::get("favorites") + ","; }
    bool isFavourite(int i) const { return favourites().find("," + std::to_string(i) + ",") != std::string::npos; }

    void toggleFavourite(int i) {
        std::string f = favourites(), key = "," + std::to_string(i) + ",";
        size_t p = f.find(key);
        if (p != std::string::npos) f.erase(p, key.size() - 1);
        else f += std::to_string(i) + ",";
        while (!f.empty() && f.front() == ',') f.erase(0, 1);
        while (!f.empty() && f.back() == ',') f.pop_back();
        config::set("favorites", f);
        config::save();
    }

    // Letters of the name in either language, folded (Turkish letters as ASCII).
    bool matches(int i) const {
        if (filter_.empty()) return true;
        std::string name = sos::fold(apps_[i]->name());
        std::string f = sos::fold(filter_);
        return name.find(f) != std::string::npos;
    }

    void rebuild() {
        int keep = selected_ < (int)visible_.size() ? visible_[selected_] : -1;
        visible_.clear();
        for (int i = 0; i < (int)apps_.size(); i++) {
            bool in = group_ == ALL || (group_ == FAVOURITES ? isFavourite(i) : (i < (int)groups_.size() && groups_[i] == group_));
            if (in && matches(i)) visible_.push_back(i);
        }
        selected_ = 0;
        for (int v = 0; v < (int)visible_.size(); v++)
            if (visible_[v] == keep) selected_ = v;
        scrollRow_ = 0;
    }
};
