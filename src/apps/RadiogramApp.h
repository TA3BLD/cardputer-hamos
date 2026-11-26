#pragma once
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/AprsIs.h"
#include "core/Config.h"
#include "core/Lang.h"
#include "core/Mesh.h"
#include "core/Net.h"
#include "core/Storage.h"
#include "core/Ui.h"

// Formal message (radiogram / ICS-style): number, precedence, origin station,
// place, time, to, text, signature; the word count (check) is counted.
// TAB / ENTER moves between fields, Fn+S sends it over the mesh (in parts of
// up to 200 characters) and to an APRS callsign if one is given, and saves it
// as /HamOS/msgs/<number>.txt.
class RadiogramApp : public App {
public:
    const char* name() const override { return T("Message form"); }
    uint16_t color() const override { return 0x6B4D; }

    void onEnter() override {
        if (fields_.empty()) reset();
    }

    void onKey(const KeyEvent& k) override {
        dirty = true;
        if (k.fn && (k.ch == 's' || k.ch == 'S')) { send(); return; }
        if (k.fn && (k.ch == 'n' || k.ch == 'N')) { reset(); return; }
        if (k.tab || k.enter || k.fnDown()) { field_ = (field_ + 1) % (int)fields_.size(); return; }
        if (k.fnUp()) { field_ = (field_ + (int)fields_.size() - 1) % (int)fields_.size(); return; }
        if (field_ == PREC && (k.ch == ' ' || k.left() || k.right())) {
            static const char* p[] = {"ROUTINE", "PRIORITY", "IMMEDIATE", "EMERGENCY"};
            int i = 0;
            while (i < 4 && fields_[PREC].value != p[i]) i++;
            fields_[PREC].value = p[(i + 1) % 4];
            return;
        }
        fields_[(size_t)field_].handle(k);
    }

    void draw() override {
        ui::clearBody();
        static const char* labels[] = {"Nr", "Precedence", "From stn", "Place", "Time", "To", "APRS to", "Text", "Signed"};
        int first = field_ > 6 ? field_ - 6 : 0;
        for (int i = first; i < (int)fields_.size() && i < first + 7; i++)
            ui::formRow(i - first, T(labels[i]), fields_[(size_t)i].value, i == field_, i == PREC && fields_[PREC].value == "EMERGENCY" ? ui::C_ERR : ui::C_TEXT);
        char b[64];
        snprintf(b, sizeof(b), "%s %d", T("check (words)"), words(fields_[TEXT].value));
        ui::text(6, ui::BODY_Y + 7 * ui::ROW_H + 6, status_.empty() ? std::string(b) : status_, status_.empty() ? ui::C_DIM : ui::C_OK);
        ui::footer(T("TAB field  Fn+S send  Fn+N new"));
    }

    std::string formatted() const {
        const auto& f = fields_;
        return "NR " + f[NR].value + " " + f[PREC].value + " " + f[FROM].value + " " + std::to_string(words(f[TEXT].value)) + " " +
               f[PLACE].value + " " + f[TIME].value + " TO " + f[TO].value + " BT " + f[TEXT].value + " BT " + f[SIG].value + " AR";
    }
    ui::TextInput& fieldForTest(int i) { return fields_[(size_t)i]; }
    enum { NR, PREC, FROM, PLACE, TIME, TO, APRS_TO, TEXT, SIG };

private:
    std::vector<ui::TextInput> fields_;
    int field_ = PLACE;
    std::string status_;

    static int words(const std::string& s) {
        int n = 0;
        bool in = false;
        for (char c : s) {
            bool w = c != ' ';
            if (w && !in) n++;
            in = w;
        }
        return n;
    }

    void reset() {
        fields_.assign(9, ui::TextInput());
        for (auto& f : fields_) { f.upper = true; f.maxLen = 40; }
        fields_[TEXT].maxLen = 160;
        int nr = atoi(config::get("msg_nr", "0").c_str()) + 1;
        fields_[NR].value = std::to_string(nr);
        fields_[PREC].value = "ROUTINE";
        fields_[FROM].value = config::get("callsign");
        if (net::timeValid()) {
            time_t t = (time_t)(net::utcMillis() / 1000);
            struct tm g;
            gmtime_r(&t, &g);
            char b[16];
            strftime(b, sizeof(b), "%H%MZ %d%b", &g);
            fields_[TIME].value = b;
        }
        field_ = PLACE;
        status_.clear();
    }

    void send() {
        std::string text = formatted();
        std::string via;
        std::string err;
        if (mesh::ready()) {
            int parts = (int)((text.size() + 189) / 190);
            bool ok = true;
            for (int i = 0; i < parts; i++) {
                std::string part = (parts > 1 ? "[" + std::to_string(i + 1) + "/" + std::to_string(parts) + "] " : "") + text.substr((size_t)i * 190, 190);
                ok &= mesh::sendText(part, err);
            }
            if (ok) via = "mesh";
        }
        std::string to = fields_[APRS_TO].value;
        if (!to.empty() && aprsis::canSend()) {
            bool ok = true;
            for (size_t i = 0; i < text.size(); i += 60) ok &= aprsis::sendMessage(to, text.substr(i, 60), err);
            if (ok) via += via.empty() ? "APRS" : ", APRS";
        }
        storage::makeDir("/HamOS/msgs");
        storage::writeFile(("/HamOS/msgs/" + fields_[NR].value + ".txt").c_str(), text + "\n");
        config::set("msg_nr", fields_[NR].value);
        config::save();
        status_ = via.empty() ? std::string(T("Saved")) + ": " + T("nothing could send") : std::string(T("Sent via")) + " " + via;
    }
};
