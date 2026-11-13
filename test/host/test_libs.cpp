// Host-side test: g++ -std=c++17 -I src test/host/test_libs.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include <cstring>
#include "lib/Adif.h"
#include "lib/Morse.h"
#include "lib/RadioMath.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) < tol; }

int main() {
    // --- ADIF ---
    CHECK(adif::band(14.074) == "20m" && adif::band(145.5) == "2m" && adif::band(433.5) == "70cm");
    CHECK(adif::band(12.0).empty());
    adif::Qso q{"TA1ABC", "14.074", "FT8", "59", "57", "20261113", "1234", "nice <qso>", "", "", ""};
    std::string rec = adif::toRecord(q);
    CHECK(rec.find("<CALL:6>TA1ABC ") == 0);
    CHECK(rec.find("<BAND:3>20m ") != std::string::npos);
    adif::Qso empty{"W1AW", "", "", "", "", "", "", "", "", "", ""};
    std::string file = adif::header() + rec + adif::toRecord(empty);
    auto all = adif::parse(file);
    CHECK(all.size() == 2);
    CHECK(all[0].call == "TA1ABC" && all[0].freq == "14.074" && all[0].mode == "FT8");
    CHECK(all[0].rstSent == "59" && all[0].rstRcvd == "57" && all[0].date == "20261113" && all[0].time == "1234");
    CHECK(all[0].comment == "nice <qso>");          // '<' inside a value must not confuse the parser
    CHECK(all[1].call == "W1AW" && all[1].freq.empty());
    // Files from other programs: lower case tags, type suffix, seconds in the time, no header
    auto other = adif::parse("<call:5>DL1XX<time_on:6>093015<freq:5:N>7.100<foo:3>bar<eor>\n<call:4>G4AB<eor>");
    CHECK(other.size() == 2 && other[0].time == "0930" && other[0].freq == "7.100" && other[1].call == "G4AB");
    CHECK(adif::parse("<CALL:99>short").empty());   // truncated file must not crash
    // Contest and activation fields, and the start time used for the hourly rate
    adif::Qso c{"EA5XX", "14.250", "SSB", "59", "59", "20261113", "1234", "", "", "", ""};
    c.stx = "001"; c.srx = "047"; c.potaRef = "TR-0042";
    std::string crec = adif::toRecord(c);
    CHECK(crec.find("<STX:3>001 <SRX:3>047 <MY_SIG:4>POTA <MY_SIG_INFO:7>TR-0042 <EOR>") != std::string::npos);
    auto cback = adif::parse(crec);
    CHECK(cback.size() == 1 && cback[0].stx == "001" && cback[0].srx == "047" && cback[0].potaRef == "TR-0042");
    CHECK(adif::startUnix(c) == 1794573240LL);      // 2026-11-13 12:34 UTC
    CHECK(adif::startUnix(empty) == 0);

    // --- Radio maths ---
    CHECK(near(radiomath::halfWaveDipoleM(14.2), 10.035, 0.01));
    CHECK(near(radiomath::wavelengthM(145.0), 2.0675, 0.001));
    CHECK(near(radiomath::swr(100, 4), 1.5, 1e-9));
    CHECK(near(radiomath::swr(100, 0), 1.0, 1e-9));
    CHECK(std::isinf(radiomath::swr(10, 10)) && std::isnan(radiomath::swr(0, 1)));
    CHECK(near(radiomath::returnLossDb(100, 1), 20.0, 1e-9));
    CHECK(near(radiomath::wattsToDbm(5), 36.99, 0.01) && near(radiomath::dbmToWatts(30), 1.0, 1e-9));
    CHECK(near(radiomath::afterLossW(100, 3.0103), 50.0, 0.01));
    CHECK(near(radiomath::resonanceMhz(10, 100), 5.033, 0.001));

    // --- Morse ---
    CHECK(!strcmp(morse::encode('a'), ".-") && !strcmp(morse::encode('S'), "...") && !strcmp(morse::encode('0'), "-----"));
    CHECK(!strcmp(morse::encode('?'), "..--..") && !strcmp(morse::encode('#'), ""));
    for (const char* p = morse::kKochOrder; *p; p++) CHECK(morse::encode(*p)[0] != 0);
    CHECK(strlen(morse::kKochOrder) == 40);
    CHECK(morse::dotMs(20) == 60 && morse::dotMs(0) == 240);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
