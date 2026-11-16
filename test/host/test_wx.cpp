// Host test: APRS digipeating rules, weather reports, BME280 compensation.
//   g++ -std=c++17 -I src test/host/test_wx.cpp -o test/host/t.out && test/host/t.out
//   With "bme" it prints values for tools/wx/check_bme280.py.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "lib/Aprs.h"
#include "lib/Bme280.h"
#include "lib/Emergency.h"
#include "lib/Awards.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

// The calibration and raw values of the datasheet's worked example (BME280
// datasheet, section 8 / BMP280 table "compensation example"): adc_T 519888,
// adc_P 415148 -> 25.08 C, 100653.27 Pa. Humidity raw 0x6C3D.
static const uint8_t kC1[26] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B, 0x8C, 0x00,
                                0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17, 0x00, 0x4B};
static const uint8_t kC2[7] = {0x5B, 0x01, 0x00, 0x16, 0x05, 0x00, 0x1E};
static const uint8_t kRaw[8] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x6C, 0x3D};

int main(int argc, char** argv) {
    bme280::Calibration c = bme280::parseCalibration(kC1, kC2);
    bme280::Reading r = bme280::compensate(c, kRaw);
    if (argc > 1 && !strcmp(argv[1], "bme")) {
        printf("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", c.t1, c.t2, c.t3, c.p1, c.p2, c.p3, c.p4, c.p5, c.p6, c.p7,
               c.p8, c.p9, c.h1, c.h2, c.h3, c.h4, c.h5, c.h6);
        printf("%.6f %.4f %.6f\n", r.celsius, r.pascal, r.humidity);
        return 0;
    }
    CHECK(c.t1 == 27504 && c.t2 == 26435 && c.t3 == -1000 && c.p1 == 36477 && c.p9 == 6000);
    CHECK(std::fabs(r.celsius - 25.08) < 0.005 && std::fabs(r.pascal - 100653.27) < 0.05);
    CHECK(r.humidity >= 0 && r.humidity <= 100);

    // Digipeating
    using aprs::digipeat;
    CHECK(digipeat("TA1ABC-7>APRS,WIDE1-1,WIDE2-1:!test", "TA3BLD-10", false) == "TA1ABC-7>APRS,TA3BLD-10,WIDE1*,WIDE2-1:!test");
    CHECK(digipeat("TA1ABC-7>APRS,WIDE2-2:!test", "TA3BLD-10", false) == "");            // fill-in only
    CHECK(digipeat("TA1ABC-7>APRS,WIDE2-2:!test", "TA3BLD-10", true) == "TA1ABC-7>APRS,TA3BLD-10*,WIDE2-1:!test");
    CHECK(digipeat("TA1ABC-7>APRS,DIGI1,WIDE1*,WIDE2-1:!t", "TA3BLD-10", true) == "TA1ABC-7>APRS,DIGI1,WIDE1,TA3BLD-10,WIDE2*:!t");
    CHECK(digipeat("TA1ABC-7>APRS,TA3BLD-10*,WIDE2-1:!t", "TA3BLD-10", true) == "");    // already repeated by us
    CHECK(digipeat("TA1ABC-7>APRS,TA3BLD-10,WIDE2-1:!t", "TA3BLD-10", false) == "TA1ABC-7>APRS,TA3BLD-10*,WIDE2-1:!t");
    CHECK(digipeat("TA3BLD-10>APRS,WIDE1-1:!own", "TA3BLD-10", true) == "");
    CHECK(digipeat("TA1ABC>APRS:!no path", "TA3BLD-10", true) == "");
    CHECK(digipeat("TA1ABC>APRS,TCPIP*:!internet", "TA3BLD-10", true) == "");

    // Weather report
    std::string wx = aprs::formatWeather(38.4237, 27.1428, 21.0, 55, 1013.2, " HamOS");
    CHECK(wx == "!3825.42N/02708.57E_.../...g...t070h55b10132 HamOS");
    CHECK(aprs::formatWeather(38.4237, 27.1428, -21.0, 100, 998.04, "").find("t-06h00b09980") != std::string::npos);
    aprs::Position pos = aprs::parsePosition(wx);
    CHECK(pos.valid && std::fabs(pos.lat - 38.4237) < 0.001);

    // Emergency
    CHECK(sos::isDistress("SOS at the lake") && sos::isDistress("mayday mayday") && sos::isDistress("Acil yardım lazım"));
    CHECK(sos::isDistress("ACİL: köprü yıkıldı") && sos::isDistress("YARDIM!") && sos::isDistress("Emergency."));
    CHECK(!sos::isDistress("SOSIS aldim") && !sos::isDistress("Yardimci operator") && !sos::isDistress("73 good dx"));
    CHECK(sos::fold("çğıİöşü") == "CGIIOSU");
    CHECK(sos::text("TA3BLD", true, 38.4237, 27.1428, "KM38nk", 87, 2, "bacak kirik") ==
          "SOS TA3BLD 38.42370,27.14280 KM38nk bat 87% #2 bacak kirik");
    CHECK(sos::text("TA3BLD", false, 0, 0, "", -1, 1, "") == "SOS TA3BLD #1");
    CHECK(sos::sendTime(0) == 0 && sos::sendTime(1) == 60 && sos::sendTime(5) == 300 && sos::sendTime(6) == 600 && sos::sendTime(7) == 900);

    // Awards
    CHECK(awards::provinceOf("Karşıyaka, İzmir") == 34 && awards::provinceOf("ISTANBUL") == 33 && awards::provinceOf("Şanlıurfa") == 62);
    CHECK(awards::provinceOf("Antep") == 26 && awards::provinceOf("Karşıyaka") == -1 && awards::provinceOf("Iğdır merkez") == 75);
    CHECK(awards::provinceOf("Kars") == 35 && awards::provinceOf("Afyon") == 2 && awards::provinceOf("Düzce") == 80);
    {
        awards::Tally t;
        adif::Qso q;
        q.call = "TA1ABC"; q.freq = "14.074"; q.qth = "Kadıköy, İstanbul"; q.grid = "km40"; t.add(q);
        q.call = "TA3XYZ"; q.freq = "7.074"; q.qth = ""; q.comment = "op Ali, Izmir"; q.grid = "KM38nk"; t.add(q);
        q.call = "DL1ABC"; q.freq = "14.074"; q.qth = "Bremen"; q.grid = "JO43"; t.add(q);
        q.call = "JA1XYZ"; q.freq = "21.074"; q.grid = "PM95"; t.add(q);
        q.call = "PY2AA"; q.freq = "28.074"; q.grid = "zz99"; t.add(q);            // not a grid
        CHECK(t.contacts == 5 && t.countries.size() == 4 && t.provinceSet.size() == 2 && t.provinceSet.count(33) && t.provinceSet.count(34));
        CHECK(t.continents.size() == 3 && t.continents.count("EU") && t.continents.count("SA"));   // AS (TA, JA), EU, SA
        CHECK(t.grids.size() == 4 && t.gridsByBand["20m"].size() == 2 && t.countriesByBand["20m"].size() == 2);
    }

    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
