#pragma once
#include <cstdio>
#include "core/App.h"
#include "core/Lang.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Maidenhead.h"

// Distance and beam heading between two Maidenhead locators.
class LocatorApp : public App {
public:
    const char* name() const override { return T("Locator"); }
    uint16_t color() const override { return 0x07E0; }

    void onEnter() override {
        from_.maxLen = to_.maxLen = 6;
        if (from_.value.empty() || from_.value == lastAuto_) from_.value = lastAuto_ = station::grid();
        active_ = from_.value.empty() ? 0 : 1;
    }

    void onKey(const KeyEvent& k) override {
        if (k.tab || k.enter) {
            active_ = 1 - active_;
            dirty = true;
        } else if ((active_ == 0 ? from_ : to_).handle(k)) {
            dirty = true;
        }
    }

    void draw() override {
        ui::clearBody();
        field(0, "FROM", from_);
        field(1, "TO", to_);

        double lat1, lon1, lat2, lon2;
        bool okFrom = maidenhead::toLatLon(from_.value, lat1, lon1);
        bool okTo = maidenhead::toLatLon(to_.value, lat2, lon2);
        char line[48];
        if (okFrom && okTo) {
            snprintf(line, sizeof(line), "%.0f km", maidenhead::distanceKm(lat1, lon1, lat2, lon2));
            ui::text(8, 58, line, ui::C_OK, 2);
            snprintf(line, sizeof(line), "%03.0f deg", maidenhead::bearingDeg(lat1, lon1, lat2, lon2));
            ui::text(130, 58, line, ui::C_OK, 2);
            snprintf(line, sizeof(line), "TO: %.3f, %.3f", lat2, lon2);
            ui::text(8, 84, line, ui::C_DIM);
        } else {
            ui::text(8, 62, T("Enter two locators (KM38 or KM38nk)"), ui::C_DIM);
        }
        ui::footer(T("TAB switch field   ` back"));
    }

private:
    void field(int index, const char* label, const ui::TextInput& in) {
        int y = ui::BODY_Y + 6 + index * 18;
        bool sel = (index == active_);
        bool ok = maidenhead::isValid(in.value);
        ui::text(8, y, label, sel ? ui::C_SEL : ui::C_DIM, 1.5);
        ui::text(70, y, in.value + (sel ? "_" : ""), ok || in.value.empty() ? ui::C_TEXT : ui::C_WARN, 1.5);
    }

    ui::TextInput from_, to_;
    std::string lastAuto_;      // the grid filled in automatically (GPS or Settings)
    int active_ = 0;
};
