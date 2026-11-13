#pragma once
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Net.h"
#include "core/Storage.h"
#include "core/Ui.h"
#include "lib/Adif.h"

// QSO logbook. The log itself is a standard ADIF file (/HamOS/log.adi) that
// can be imported straight into any PC logging program.
class LogbookApp : public App {
public:
    const char* name() const override { return "Logbook"; }
    uint16_t color() const override { return 0xF800; }

    void onEnter() override {
        if (!loaded_) load();
        editing_ = false;
        status_.clear();
    }

    bool onBack() override {
        if (!editing_) return false;
        editing_ = false;
        dirty = true;
        return true;
    }

    void onKey(const KeyEvent& k) override {
        if (editing_) {
            if (k.enter && field_ == FIELDS - 1) save();
            else if (k.enter || k.tab) {
                if (field_ == MODE) defaultRst();
                field_ = (field_ + 1) % FIELDS;
            }
            else if (!in_[field_].handle(k)) return;
            dirty = true;
        } else if (k.ch == 'n' || k.ch == 'N') {
            startForm();
            dirty = true;
        } else if (ui::listNav(k, selected_, (int)qsos_.size())) {
            dirty = true;
        }
    }

    void draw() override {
        if (editing_) drawForm();
        else drawList();
    }

private:
    enum { CALL, FREQ, MODE, SENT, RCVD, NOTE, FIELDS };
    static constexpr size_t MAX_IN_MEMORY = 200;   // RAM limit; the file keeps everything
    static constexpr const char* kPath = "/HamOS/log.adi";

    void load() {
        std::string text;
        if (storage::readFile(kPath, text)) {
            qsos_ = adif::parse(text);
            total_ = qsos_.size();
            if (qsos_.size() > MAX_IN_MEMORY) qsos_.erase(qsos_.begin(), qsos_.end() - MAX_IN_MEMORY);
        }
        loaded_ = true;
    }

    void startForm() {
        static const size_t maxLen[FIELDS] = {12, 10, 8, 3, 3, 40};
        for (int i = 0; i < FIELDS; i++) {
            in_[i] = ui::TextInput();
            in_[i].maxLen = maxLen[i];
        }
        in_[CALL].upper = in_[MODE].upper = true;
        // Frequency and mode usually stay the same between contacts.
        in_[FREQ].value = qsos_.empty() ? "14.200" : qsos_.back().freq;
        in_[MODE].value = qsos_.empty() ? "SSB" : qsos_.back().mode;
        defaultRst();
        field_ = CALL;
        editing_ = true;
        status_.clear();
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
        q.comment = in_[NOTE].value;
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
        std::string out = storage::exists(kPath) ? "" : adif::header();
        bool ok = storage::appendFile(kPath, out + adif::toRecord(q));
        qsos_.push_back(q);
        total_++;
        if (qsos_.size() > MAX_IN_MEMORY) qsos_.erase(qsos_.begin());
        status_ = ok ? "Saved " + q.call : "No SD card: lost at power-off";
        selected_ = 0;
        editing_ = false;
    }

    // How many logged contacts share this callsign; `sameBandMode` counts true dupes.
    int workedBefore(const std::string& call, int& sameBandMode) const {
        int n = 0;
        sameBandMode = 0;
        std::string band = adif::band(atof(in_[FREQ].value.c_str()));
        for (const adif::Qso& q : qsos_) {
            if (q.call != call) continue;
            n++;
            if (q.mode == in_[MODE].value && adif::band(atof(q.freq.c_str())) == band) sameBandMode++;
        }
        return n;
    }

    void drawForm() {
        static const char* labels[FIELDS] = {"Callsign", "Freq MHz", "Mode", "RST sent", "RST rcvd", "Note"};
        ui::clearBody();
        for (int i = 0; i < FIELDS; i++) ui::formRow(i, labels[i], in_[i].value, i == field_);

        std::string band = adif::band(atof(in_[FREQ].value.c_str()));
        ui::text(6, ui::BODY_Y + 80, band.empty() ? "out of band" : band, band.empty() ? ui::C_WARN : ui::C_OK, 1.5);
        int dupes = 0;
        int worked = in_[CALL].value.empty() ? 0 : workedBefore(in_[CALL].value, dupes);
        if (dupes) ui::text(84, ui::BODY_Y + 80, "DUPE!", ui::C_ERR, 1.5);
        else if (worked) ui::text(84, ui::BODY_Y + 83, "worked " + std::to_string(worked) + "x before", ui::C_WARN);
        else if (!net::timeValid()) ui::text(84, ui::BODY_Y + 83, "no UTC time yet", ui::C_WARN);
        ui::footer(!status_.empty() ? status_.c_str()
                   : field_ == FIELDS - 1 ? "ENTER save QSO   ` cancel" : "ENTER/TAB next   ` cancel");
    }

    void drawList() {
        if (qsos_.empty()) {
            ui::clearBody();
            ui::textCentered(ui::BODY_Y + 30, "Log is empty", ui::C_DIM, 2);
            ui::textCentered(ui::BODY_Y + 62, "Press N to log your first QSO", ui::C_DIM);
        } else {
            std::vector<std::string> rows;
            char line[48];
            for (auto it = qsos_.rbegin(); it != qsos_.rend(); ++it) {   // newest first
                std::string day = it->date.size() == 8 ? it->date.substr(6, 2) + "." + it->date.substr(4, 2) : "--.--";
                snprintf(line, sizeof(line), "%s %-4s %-9s %-7s %-4s %s", day.c_str(),
                         it->time.empty() ? "----" : it->time.c_str(), it->call.c_str(), it->freq.c_str(),
                         it->mode.c_str(), it->rstSent.c_str());
                rows.push_back(line);
            }
            ui::list(rows, selected_, scroll_);
        }
        std::string hint = !status_.empty() ? status_ : "N new QSO   " + std::to_string(total_) + " in log";
        ui::footer(hint.c_str());
    }

    std::vector<adif::Qso> qsos_;
    size_t total_ = 0;
    bool loaded_ = false, editing_ = false;
    int selected_ = 0, scroll_ = 0, field_ = 0;
    ui::TextInput in_[FIELDS];
    std::string status_;
};
