#pragma once
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Log.h"
#include "core/Net.h"
#include "core/Rig.h"
#include "core/Station.h"
#include "core/Ui.h"
#include "lib/Adif.h"
#include "lib/Dxcc.h"
#include "lib/Maidenhead.h"

// QSO logbook. The log itself is a standard ADIF file (/HamOS/log.adi) that
// can be imported straight into any PC logging program. While a callsign is
// typed the form shows its country, distance and beam heading.
class LogbookApp : public App {
public:
    const char* name() const override { return "Logbook"; }
    uint16_t color() const override { return 0xF800; }

    void onEnter() override {
        logbook::load();
        editing_ = confirmDelete_ = false;
        status_.clear();
    }

    bool onBack() override {
        if (!editing_ && !confirmDelete_) return false;
        editing_ = confirmDelete_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        const auto& qsos = logbook::recent();
        if (editing_) {
            if (k.enter && field_ == FIELDS - 1) save();
            else if (k.enter || k.tab) {
                if (field_ == MODE) defaultRst();
                field_ = (field_ + 1) % FIELDS;
            }
            else if (!in_[field_].handle(k)) return;
            dirty = true;
        } else if (confirmDelete_) {
            if (k.ch == 'y' || k.ch == 'Y') {
                size_t index = qsos.size() - 1 - (size_t)selected_;     // list shows newest first
                std::string call = qsos[index].call;
                status_ = logbook::remove(index) ? "Deleted " + call : "Delete failed";
                if (selected_ >= (int)logbook::recent().size()) selected_ = 0;
            }
            confirmDelete_ = false;
            dirty = true;
        } else if (k.ch == 'n' || k.ch == 'N') {
            startForm();
            dirty = true;
        } else if ((k.ch == 'd' || k.ch == 'D') && !qsos.empty()) {
            confirmDelete_ = true;
            dirty = true;
        } else if (ui::listNav(k, selected_, (int)qsos.size())) {
            status_.clear();
            dirty = true;
        }
    }

    void draw() override {
        if (editing_) drawForm();
        else drawList();
    }

private:
    enum { CALL, FREQ, MODE, SENT, RCVD, NOTE, FIELDS };

    void startForm() {
        static const size_t maxLen[FIELDS] = {12, 10, 8, 3, 3, 40};
        for (int i = 0; i < FIELDS; i++) {
            in_[i] = ui::TextInput();
            in_[i].maxLen = maxLen[i];
        }
        in_[CALL].upper = in_[MODE].upper = true;
        // Frequency and mode usually stay the same between contacts.
        const auto& qsos = logbook::recent();
        in_[FREQ].value = qsos.empty() ? "14.200" : qsos.back().freq;
        in_[MODE].value = qsos.empty() ? "SSB" : qsos.back().mode;
        if (rig::connected()) {             // the radio knows better than the last contact
            in_[FREQ].value = rig::freqMhz();
            if (!rig::mode().empty()) in_[MODE].value = rig::mode();
        }
        defaultRst();
        field_ = CALL;
        editing_ = true;
        status_.clear();
    }

    static bool contest() { return config::get("contest", "0") == "1"; }

    // Next serial number to send: one more than the highest one in the recent log.
    static std::string nextSerial() {
        int highest = 0;
        for (const adif::Qso& q : logbook::recent())
            if (atoi(q.stx.c_str()) > highest) highest = atoi(q.stx.c_str());
        char buf[16];
        snprintf(buf, sizeof(buf), "%03d", highest + 1);
        return buf;
    }

    // Contacts in the last 60 minutes.
    static int lastHour() {
        if (!net::timeValid()) return 0;
        long long now = (long long)time(nullptr);
        int n = 0;
        for (const adif::Qso& q : logbook::recent()) {
            long long t = adif::startUnix(q);
            if (t && now - t >= 0 && now - t < 3600) n++;
        }
        return n;
    }

    // CW reports have a third (tone) digit. Only touches reports still at their default.
    void defaultRst() {
        const char* rst = (in_[MODE].value == "CW") ? "599" : "59";
        for (int i : {SENT, RCVD})
            if (in_[i].value.empty() || in_[i].value == "59" || in_[i].value == "599") in_[i].value = rst;
    }

    void save() {
        if (in_[CALL].value.empty()) {
            status_ = "Callsign required";
            field_ = CALL;
            return;
        }
        adif::Qso q;
        q.call = in_[CALL].value;
        q.freq = in_[FREQ].value;
        q.mode = in_[MODE].value;
        q.rstSent = in_[SENT].value;
        q.rstRcvd = in_[RCVD].value;
        if (contest()) {                    // the last field is the number received; ours counts up
            q.srx = in_[NOTE].value;
            q.stx = nextSerial();
        } else {
            q.comment = in_[NOTE].value;
        }
        q.potaRef = config::get("pota_ref");
        if (net::timeValid()) {
            time_t now = time(nullptr);
            struct tm t;
            gmtime_r(&now, &t);
            char buf[16];
            strftime(buf, sizeof(buf), "%Y%m%d", &t);
            q.date = buf;
            strftime(buf, sizeof(buf), "%H%M", &t);
            q.time = buf;
        }
        bool ok = logbook::add(q);
        status_ = ok ? "Saved " + q.call : "No SD card: lost at power-off";
        selected_ = 0;
        editing_ = false;
    }

    void drawForm() {
        static const char* labels[FIELDS] = {"Callsign", "Freq MHz", "Mode", "RST sent", "RST rcvd", "Note"};
        ui::clearBody();
        for (int i = 0; i < FIELDS; i++)
            ui::formRow(i, (i == NOTE && contest()) ? "Nr rcvd" : labels[i], in_[i].value, i == field_);
        if (contest()) ui::text(150, ui::BODY_Y + 2 + SENT * ui::ROW_H + 2, "send nr " + nextSerial(), ui::C_OK);

        // Line 1: country, distance and heading from the callsign prefix.
        const dxcc::Entity* e = dxcc::lookup(in_[CALL].value);
        if (e) {
            std::string where = e->name;
            double lat, lon;
            if (station::position(lat, lon)) {
                char buf[40];
                snprintf(buf, sizeof(buf), "  %.0f km  %03.0f deg", maidenhead::distanceKm(lat, lon, e->lat, e->lon),
                         maidenhead::bearingDeg(lat, lon, e->lat, e->lon));
                where += buf;
            }
            ui::text(6, ui::BODY_Y + 77, where, logbook::workedEntity(e) ? ui::C_TEXT : ui::C_SEL);
            if (!logbook::workedEntity(e)) ui::text(ui::W - 30, ui::BODY_Y + 77, "NEW", ui::C_SEL);
        }
        // Line 2: band and warnings.
        std::string band = adif::band(atof(in_[FREQ].value.c_str()));
        ui::text(6, ui::BODY_Y + 90, band.empty() ? "out of band" : band, band.empty() ? ui::C_WARN : ui::C_OK, 1.5);
        int dupes = 0;
        int worked = in_[CALL].value.empty() ? 0 : logbook::worked(in_[CALL].value, in_[FREQ].value, in_[MODE].value, dupes);
        if (dupes) ui::text(84, ui::BODY_Y + 90, "DUPE!", ui::C_ERR, 1.5);
        else if (worked) ui::text(84, ui::BODY_Y + 93, "worked " + std::to_string(worked) + "x before", ui::C_WARN);
        else if (!net::timeValid()) ui::text(84, ui::BODY_Y + 93, "no UTC time yet", ui::C_WARN);
        ui::footer(!status_.empty() ? status_.c_str()
                   : field_ == FIELDS - 1 ? "ENTER save QSO   ` cancel" : "ENTER/TAB next   ` cancel");
    }

    void drawList() {
        const auto& qsos = logbook::recent();
        if (qsos.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 30, "Log is empty", ui::C_DIM, 2);
            ui::textCentered(ui::BODY_Y + 62, "Press N to log your first QSO", ui::C_DIM);
        } else {
            std::vector<std::string> rows;
            char line[48];
            for (auto it = qsos.rbegin(); it != qsos.rend(); ++it) {   // newest first
                std::string day = it->date.size() == 8 ? it->date.substr(6, 2) + "." + it->date.substr(4, 2) : "--.--";
                snprintf(line, sizeof(line), "%s %-4s %-9s %-7s %-4s %s", day.c_str(),
                         it->time.empty() ? "----" : it->time.c_str(), it->call.c_str(), it->freq.c_str(),
                         it->mode.c_str(), it->rstSent.c_str());
                rows.push_back(line);
            }
            ui::list(rows, selected_, scroll_);
        }
        std::string hint;
        if (confirmDelete_) hint = "Delete " + qsos[qsos.size() - 1 - (size_t)selected_].call + "?  Y yes   any key no";
        else if (!status_.empty()) hint = status_;
        else if (contest()) {
            hint = "N new  D del  " + std::to_string(logbook::total()) + " QSOs  " + std::to_string(lastHour()) + "/h";
        } else {
            hint = "N new  D delete  " + std::to_string(logbook::total()) + " QSOs";
            if (!config::get("pota_ref").empty()) hint += "  " + config::get("pota_ref");
            else if (rig::connected()) hint += "  RIG";
        }
        ui::footer(hint.c_str());
    }

    bool editing_ = false, confirmDelete_ = false;
    int selected_ = 0, scroll_ = 0, field_ = 0;
    ui::TextInput in_[FIELDS];
    std::string status_;
};
