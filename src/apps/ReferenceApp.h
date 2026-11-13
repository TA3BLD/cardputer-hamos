#pragma once
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Ui.h"

// Quick reference tables. TAB cycles pages, arrows scroll.
class ReferenceApp : public App {
public:
    const char* name() const override { return "Reference"; }

    void onKey(const KeyEvent& k) override {
        if (k.tab || k.right()) page_ = (page_ + 1) % (int)pages().size();
        else if (k.left()) page_ = (page_ + (int)pages().size() - 1) % (int)pages().size();
        else if (!ui::listNav(k, selected_, (int)pages()[page_].lines.size())) return;
        if (k.tab || k.left() || k.right()) selected_ = scroll_ = 0;
        dirty = true;
    }

    void draw() override {
        const Page& p = pages()[page_];
        ui::list(p.lines, selected_, scroll_);
        std::string hint = std::string("[") + p.title + "]  TAB next page   ` back";
        ui::footer(hint.c_str());
    }

private:
    struct Page {
        const char* title;
        std::vector<std::string> lines;
    };

    static const std::vector<Page>& pages() {
        static const std::vector<Page> p = {
            {"Q codes",
             {"QRA  Station name", "QRG  Exact frequency", "QRL  Frequency in use / busy",
              "QRM  Man-made interference", "QRN  Static / atmospheric noise", "QRO  Increase power",
              "QRP  Reduce power / low power", "QRQ  Send faster", "QRS  Send slower",
              "QRT  Stop transmitting", "QRV  I am ready", "QRX  Stand by", "QRZ  Who is calling me?",
              "QSB  Signal fading", "QSL  I confirm receipt", "QSO  Contact", "QSY  Change frequency",
              "QTH  Location", "QTR  Exact time"}},
            {"Phonetic",
             {"A Alfa      N November", "B Bravo     O Oscar", "C Charlie   P Papa", "D Delta     Q Quebec",
              "E Echo      R Romeo", "F Foxtrot   S Sierra", "G Golf      T Tango", "H Hotel     U Uniform",
              "I India     V Victor", "J Juliett   W Whiskey", "K Kilo      X X-ray", "L Lima      Y Yankee",
              "M Mike      Z Zulu"}},
            {"RST",
             {"R1 Unreadable", "R2 Barely readable", "R3 Readable with difficulty",
              "R4 Readable, almost no difficulty", "R5 Perfectly readable", "S1 Faint, barely perceptible",
              "S3 Weak", "S5 Fairly good", "S7 Moderately strong", "S9 Very strong",
              "T1 Very rough AC tone (CW)", "T5 Filtered, strongly rippled", "T9 Perfect tone"}},
            {"HF bands",
             {"160m  1.810 - 2.000 MHz", "80m   3.500 - 3.800 MHz", "40m   7.000 - 7.200 MHz",
              "30m   10.100 - 10.150 MHz", "20m   14.000 - 14.350 MHz", "17m   18.068 - 18.168 MHz",
              "15m   21.000 - 21.450 MHz", "12m   24.890 - 24.990 MHz", "10m   28.000 - 29.700 MHz",
              "6m    50.000 - 52.000 MHz", "2m    144 - 146 MHz", "70cm  430 - 440 MHz",
              "(IARU Region 1 - check local rules)"}},
        };
        return p;
    }

    int page_ = 0, selected_ = 0, scroll_ = 0;
};
