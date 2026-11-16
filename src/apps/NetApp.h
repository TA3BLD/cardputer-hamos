#pragma once
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Net.h"
#include "core/Storage.h"
#include "core/Ui.h"

// Net control: the check-in list of a club or emergency net.
//   type a call (+ a note) and ENTER: checked in, with the time
//   Fn+T: has traffic / no traffic   Fn+X: handled   Fn+N: new net
// Each net is saved as /HamOS/nets/<date>-<time>.txt after every change.
class NetApp : public App {
public:
    const char* name() const override { return T("Net control"); }
    uint16_t color() const override { return 0xA254; }

    struct Station {
        std::string call, note, time;
        bool traffic = false, handled = false;
    };
    const std::vector<Station>& stationsForTest() const { return list_; }

    void onEnter() override {
        input_.upper = true;
        input_.maxLen = 30;
        if (file_.empty()) newNet();
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.fn && (k.ch == 'n' || k.ch == 'N')) { newNet(); return; }
        if (k.fn && (k.ch == 't' || k.ch == 'T') && !list_.empty()) { cur().traffic = !cur().traffic; save(); return; }
        if (k.fn && (k.ch == 'x' || k.ch == 'X') && !list_.empty()) { cur().handled = !cur().handled; save(); return; }
        if (k.enter) {
            if (input_.value.empty()) return;
            Station s;
            size_t sp = input_.value.find(' ');
            s.call = input_.value.substr(0, sp);
            if (sp != std::string::npos) s.note = input_.value.substr(sp + 1);
            s.traffic = s.note.find("TRAFFIC") != std::string::npos || s.note.find("TRAFIK") != std::string::npos;
            s.time = now();
            for (const Station& o : list_)
                if (o.call == s.call) { input_.value.clear(); return; }     // already in
            list_.push_back(s);
            selected_ = (int)list_.size() - 1;
            input_.value.clear();
            save();
            return;
        }
        if (ui::listNav(k, selected_, (int)list_.size())) return;
        input_.handle(k);
    }

    void draw() override {
        ui::clearBody();
        int traffic = 0;
        for (const Station& s : list_) traffic += s.traffic && !s.handled;
        char head[64];
        snprintf(head, sizeof(head), "%s %s  %u %s  %d %s", T("Net"), started_.c_str(), (unsigned)list_.size(), T("in"), traffic, T("traffic"));
        ui::fill(0, ui::BODY_Y, ui::W, 12, ui::C_FOOT);
        ui::text(4, ui::BODY_Y + 2, head, ui::C_NOTICE);
        std::vector<std::string> rows;
        std::vector<uint16_t> colors;
        for (const Station& s : list_) {
            rows.push_back(s.time + " " + (s.traffic ? (s.handled ? "v " : "! ") : "  ") + s.call + " " + s.note);
            colors.push_back(s.traffic && !s.handled ? ui::C_WARN : s.handled ? ui::C_DIM : ui::C_TEXT);
        }
        ui::list(rows, selected_, scroll_, &colors, ui::BODY_Y + 13, ui::ROWS - 2);
        ui::text(4, ui::H - ui::FOOTER_H - 11, "> " + input_.value + "_", ui::C_OK);
        ui::footer(T("ENTER in  Fn+T traffic  Fn+X ok  Fn+N new"));
    }

private:
    std::vector<Station> list_;
    ui::TextInput input_;
    int selected_ = 0, scroll_ = 0;
    std::string file_, started_;

    Station& cur() { return list_[(size_t)(selected_ < (int)list_.size() ? selected_ : 0)]; }

    static std::string now() {
        if (!net::timeValid()) return "--:--";
        time_t t = (time_t)(net::utcMillis() / 1000);
        struct tm g;
        gmtime_r(&t, &g);
        char b[8];
        strftime(b, sizeof(b), "%H:%M", &g);
        return b;
    }

    void newNet() {
        list_.clear();
        selected_ = scroll_ = 0;
        started_ = now();
        char b[48] = "net";
        if (net::timeValid()) {
            time_t t = (time_t)(net::utcMillis() / 1000);
            struct tm g;
            gmtime_r(&t, &g);
            strftime(b, sizeof(b), "%Y%m%d-%H%M", &g);
        }
        file_ = std::string("/HamOS/nets/") + b + ".txt";
    }

    void save() {
        std::string out = "Net " + started_ + " UTC\n";
        for (const Station& s : list_)
            out += s.time + "  " + s.call + (s.traffic ? "  TRAFFIC" : "") + (s.handled ? " (done)" : "") + (s.note.empty() ? "" : "  " + s.note) + "\n";
        storage::makeDir("/HamOS/nets");
        storage::writeFile(file_.c_str(), out);
    }
};
