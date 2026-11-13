#pragma once
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include "core/App.h"
#include "core/Ui.h"
#include "lib/RadioMath.h"

// Radio calculators. Results update live while typing.
class CalcApp : public App {
public:
    const char* name() const override { return "Calculator"; }
    uint16_t color() const override { return 0x07FF; }

    void onEnter() override { open_ = false; }

    bool onBack() override {
        if (!open_) return false;
        open_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (!open_) {
            if (k.enter) {
                const Calc& c = calcs()[selected_];
                for (int i = 0; i < c.inputs; i++) {
                    in_[i] = ui::TextInput();
                    in_[i].maxLen = 10;
                    in_[i].value = c.defaults[i];
                }
                field_ = 0;
                open_ = true;
            } else if (!ui::listNav(k, selected_, (int)calcs().size())) {
                return;
            }
            dirty = true;
            return;
        }
        const Calc& c = calcs()[selected_];
        if (k.enter || k.tab) field_ = (field_ + 1) % c.inputs;
        else if (k.del || (k.ch >= '0' && k.ch <= '9') || k.ch == '.' || k.ch == '-') {
            if (!in_[field_].handle(k)) return;
        } else {
            return;
        }
        dirty = true;
    }

    void draw() override {
        if (!open_) {
            std::vector<std::string> names;
            for (const Calc& c : calcs()) names.push_back(c.name);
            ui::list(names, selected_, scroll_);
            ui::footer("ENTER open   ` back");
            return;
        }
        const Calc& c = calcs()[selected_];
        ui::clearBody();
        double v[2] = {0, 0};
        for (int i = 0; i < c.inputs; i++) {
            ui::formRow(i, c.labels[i], in_[i].value, i == field_);
            v[i] = atof(in_[i].value.c_str());
        }
        Results r;
        c.compute(v, r);
        int y = ui::BODY_Y + 8 + c.inputs * ui::ROW_H;
        for (const auto& line : r) {
            ui::text(6, y + 4, line.first, ui::C_DIM);
            ui::text(100, y, line.second, ui::C_OK, 1.5);
            y += 17;
        }
        std::string hint = std::string(c.name) + (c.inputs > 1 ? "   TAB next   ` back" : "   ` back");
        ui::footer(hint.c_str());
    }

private:
    typedef std::vector<std::pair<std::string, std::string>> Results;
    struct Calc {
        const char* name;
        int inputs;
        const char* labels[2];
        const char* defaults[2];
        void (*compute)(const double* in, Results& out);
    };

    static const std::vector<Calc>& calcs() {
        using namespace radiomath;
        static const std::vector<Calc> c = {
            {"Antenna length", 1, {"Freq MHz", ""}, {"14.2", ""},
             [](const double* in, Results& out) {
                 if (in[0] <= 0) return;
                 out.push_back({"Wavelength", ui::num(wavelengthM(in[0])) + " m"});
                 out.push_back({"Dipole 1/2", ui::num(halfWaveDipoleM(in[0])) + " m"});
                 out.push_back({"Each leg", ui::num(halfWaveDipoleM(in[0]) / 2) + " m"});
                 out.push_back({"Vertical 1/4", ui::num(quarterWaveM(in[0])) + " m"});
             }},
            {"SWR from power", 2, {"Forward W", "Reflected W"}, {"100", "4"},
             [](const double* in, Results& out) {
                 if (in[0] <= 0) return;
                 out.push_back({"SWR", ui::num(swr(in[0], in[1])) + " : 1"});
                 out.push_back({"Return loss", ui::num(returnLossDb(in[0], in[1]), 1) + " dB"});
                 out.push_back({"Reflected", ui::num(100.0 * in[1] / in[0], 1) + " %"});
             }},
            {"Watt -> dBm", 1, {"Power W", ""}, {"5", ""},
             [](const double* in, Results& out) {
                 if (in[0] <= 0) return;
                 out.push_back({"dBm", ui::num(wattsToDbm(in[0]))});
                 out.push_back({"dBW", ui::num(wattsToDbm(in[0]) - 30)});
             }},
            {"dBm -> Watt", 1, {"Level dBm", ""}, {"30", ""},
             [](const double* in, Results& out) {
                 out.push_back({"Watt", ui::num(dbmToWatts(in[0]), 4)});
                 out.push_back({"Milliwatt", ui::num(dbmToWatts(in[0]) * 1000, 3)});
             }},
            {"Feedline loss", 2, {"Power in W", "Loss dB"}, {"100", "3"},
             [](const double* in, Results& out) {
                 if (in[0] <= 0) return;
                 double left = afterLossW(in[0], in[1]);
                 out.push_back({"At antenna", ui::num(left) + " W"});
                 out.push_back({"Lost", ui::num(100.0 * (1 - left / in[0]), 1) + " %"});
             }},
            {"LC resonance", 2, {"L uH", "C pF"}, {"10", "100"},
             [](const double* in, Results& out) {
                 if (in[0] <= 0 || in[1] <= 0) return;
                 out.push_back({"Frequency", ui::num(resonanceMhz(in[0], in[1]), 3) + " MHz"});
             }},
            {"Ohm's law", 2, {"Volts", "Ohms"}, {"13.8", "50"},
             [](const double* in, Results& out) {
                 if (in[1] <= 0) return;
                 out.push_back({"Current", ui::num(in[0] / in[1], 3) + " A"});
                 out.push_back({"Power", ui::num(in[0] * in[0] / in[1]) + " W"});
             }},
        };
        return c;
    }

    bool open_ = false;
    int selected_ = 0, scroll_ = 0, field_ = 0;
    ui::TextInput in_[2];
};
