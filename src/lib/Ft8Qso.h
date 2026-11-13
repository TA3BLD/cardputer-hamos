// The standard FT8 contact sequence, as WSJT-X runs it automatically:
//
//   them: CQ DX1ABC FN42          us:   DX1ABC TA3BLD KM38      (call)
//   them: TA3BLD DX1ABC -12       us:   DX1ABC TA3BLD R-07      (roger + report)
//   them: TA3BLD DX1ABC RR73      us:   DX1ABC TA3BLD 73        -> logged
//
//   us:   CQ TA3BLD KM38          them: TA3BLD DX1ABC FN42
//   us:   DX1ABC TA3BLD -07       them: TA3BLD DX1ABC R-12
//   us:   DX1ABC TA3BLD RR73      -> logged             them: TA3BLD DX1ABC 73
//
// Pure C++, host-testable.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "Ft8.h"

namespace ft8 {

struct Qso {
    enum Step { IDLE, CQ, CALL, REPORT, ROGER_REPORT, RR73, SEVENTY_THREE, DONE };
    Step step = IDLE;
    std::string myCall, myGrid;             // grid: 4 characters
    std::string dxCall, dxGrid;
    int reportSent = 0;                     // their signal here, dB
    int reportRcvd = 0;                     // our signal there, dB
    bool haveReportRcvd = false;
    bool logged = false;
    int unanswered = 0;                     // transmissions of the same step without progress

    static std::string report(int db) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%+03d", db < -30 ? -30 : db > 49 ? 49 : db);
        return buf;
    }

    // What to send now, "" when there is nothing to send.
    std::string message() const {
        switch (step) {
            case CQ: return "CQ " + myCall + (myGrid.size() >= 4 ? " " + myGrid.substr(0, 4) : "");
            case CALL: return dxCall + " " + myCall + (myGrid.size() >= 4 ? " " + myGrid.substr(0, 4) : "");
            case REPORT: return dxCall + " " + myCall + " " + report(reportSent);
            case ROGER_REPORT: return dxCall + " " + myCall + " R" + report(reportSent);
            case RR73: return dxCall + " " + myCall + " RR73";
            case SEVENTY_THREE: return dxCall + " " + myCall + " 73";
            default: return "";
        }
    }

    void callCq() { restart(CQ); }

    // Answer a station heard calling CQ (or calling us).
    void answer(const std::string& call, int snr) {
        restart(CALL);
        dxCall = call;
        reportSent = snr;
    }

    void stop() { restart(IDLE); }

    // After each of our transmissions.
    void transmitted() {
        unanswered++;
        if (step == SEVENTY_THREE) step = DONE;     // 73 is sent once
    }

    // A message decoded in the last receive period. Returns true when the
    // contact moved on; `logNow` is set at the moment it is complete.
    bool received(const std::string& text, int snr, bool& logNow) {
        logNow = false;
        std::vector<std::string> w = splitWords(text);
        if (w.size() < 2 || w[0] != myCall || myCall.empty()) return false;
        const std::string& from = w[1];
        std::string last = w.size() >= 3 ? w.back() : "";
        int db = 0;
        bool roger = false, isReport = parseReport(last, db, roger);
        bool isGrid = isGrid4(last) && last != "RR73";
        Step before = step;

        if (step == CQ || step == IDLE) {
            if (step == IDLE || from.empty() || from[0] == '<') return false;
            dxCall = from;                          // someone answered our CQ
            reportSent = snr;
            if (isGrid) dxGrid = last;
            if (isReport) { reportRcvd = db; haveReportRcvd = true; }
            step = isReport && roger ? RR73 : isReport ? ROGER_REPORT : REPORT;
        } else {
            if (from != dxCall) return false;       // someone else
            if (isGrid && dxGrid.empty()) dxGrid = last;
            if (isReport) { reportRcvd = db; haveReportRcvd = true; }
            if (step == CALL || step == REPORT) {
                if (isReport && roger) step = RR73;
                else if (isReport) step = ROGER_REPORT;
                else if ((last == "RR73" || last == "RRR") && step == REPORT) step = SEVENTY_THREE;
            } else if (step == ROGER_REPORT) {
                if (last == "RR73" || last == "RRR") step = SEVENTY_THREE;
                else if (last == "73") step = DONE;
            } else if (step == RR73) {
                if (last == "73") step = DONE;
            }
        }
        if (step == before) return false;
        unanswered = 0;
        // Complete once both reports have been exchanged and confirmed.
        if (!logged && (step == RR73 || step == SEVENTY_THREE || step == DONE) && haveReportRcvd) {
            logged = true;
            logNow = true;
        }
        return true;
    }

private:
    void restart(Step s) {
        Qso fresh;
        fresh.myCall = myCall;
        fresh.myGrid = myGrid;
        fresh.step = s;
        *this = fresh;
    }
};

}  // namespace ft8
