// Host-side test: g++ -std=c++17 -I src test/host/test_sat.cpp -o test/host/t.out && test/host/t.out
// Reference positions were produced with the python "sgp4" package (WGS-72).
#include <cstdio>
#include "lib/Sgp4.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

struct Ref { double t, x, y, z; };

static void compare(const char* name, const char* l1, const char* l2, const Ref* refs, int n) {
    sgp4::Satellite s = sgp4::fromTle(name, l1, l2);
    CHECK(s.valid);
    for (int i = 0; i < n; i++) {
        double r[3] = {0, 0, 0};
        bool ok = sgp4::propagate(s, refs[i].t, r);
        double err = std::sqrt(std::pow(r[0] - refs[i].x, 2) + std::pow(r[1] - refs[i].y, 2) + std::pow(r[2] - refs[i].z, 2));
        if (!ok || err > 0.001) printf("  %s t=%.1f: error %.6f km\n", name, refs[i].t, err);
        CHECK(ok && err < 0.001);       // within one metre of the reference implementation
    }
}

int main(int argc, char**) {
    const char* iss1 = "1 25544U          26300.50000000  .00000000  00000-0  30000-3 0    05";
    const char* iss2 = "2 25544  51.6400 100.0000 0005000  90.0000 270.0000 15.50000000    03";
    const Ref iss[] = {{0.0, -1168.714819, 6694.353989, -14.523556}, {97.3, -2315.037545, 6190.214936, 1569.288208},
                       {1440.0, 882.413045, -6728.585799, -362.950532}, {10000.0, 3216.614517, -2930.105499, -5227.566464}};
    compare("ISS", iss1, iss2, iss, 4);
    const Ref so50[] = {{0.0, -6567.372289, -2420.365807, 59.271021}, {97.3, -6593.208107, -2347.660903, -49.734266},
                        {1440.0, -1221.640115, 2763.336881, -6298.284715}, {10000.0, 7034.134499, -279.889684, 267.533131}};
    compare("SO-50", "1 27607U          26310.25000000  .00000000  00000-0  50000-4 0    03",
            "2 27607  64.5500 200.0000 0065000 300.0000  60.0000 14.76000000    04", so50, 4);
    const Ref ao7[] = {{0.0, 6774.218624, -3906.077381, -20.617205}, {97.3, 4526.424284, -1073.763367, -6298.572061},
                       {1440.0, -6520.361566, 4017.326153, -1673.661628}, {10000.0, 6713.028217, -3355.398243, 2186.626673}};
    compare("AO-7", "1 07530U          26305.75000000  .00000000  00000-0  10000-3 0    00",
            "2 07530 101.9000 330.0000 0012000  20.0000 340.0000 12.54000000    08", ao7, 4);

    // Malformed or unsupported elements are rejected, not crashed on.
    CHECK(!sgp4::fromTle("x", "", "").valid && !sgp4::fromTle("x", "garbage", "more garbage").valid);
    CHECK(!sgp4::fromTle("GEO", iss1, "2 25544   0.0500 100.0000 0005000  90.0000 270.0000  1.00270000    03").valid);

    // Epoch: day 300.5 of 2026 = 27 Oct 2026 12:00 UTC.
    sgp4::Satellite s = sgp4::fromTle("ISS", iss1, iss2);
    CHECK(std::fabs(s.epochUnix - (sgp4::daysFromCivil(2026, 10, 27) * 86400.0 + 43200.0)) < 1e-3);

    // Passes over Izmir in the two days after the epoch: plausible for the ISS.
    double t = s.epochUnix;
    int passes = 0;
    for (int i = 0; i < 20; i++) {
        sgp4::Pass p = sgp4::nextPass(s, t, 38.42, 27.14, 100, 48);
        if (!p.found) break;
        if (argc > 1) printf("PASS %.0f %.0f %.0f %.2f %.1f %.1f\n", p.aos, p.tca, p.los, p.maxElevation, p.aosAzimuth, p.losAzimuth);
        CHECK(p.los > p.aos && p.los - p.aos < 12 * 60 && p.tca >= p.aos && p.tca <= p.los);
        CHECK(p.maxElevation > 0 && p.maxElevation <= 90);
        sgp4::Look rise = sgp4::look(s, p.aos, 38.42, 27.14, 100), top = sgp4::look(s, p.tca, 38.42, 27.14, 100);
        CHECK(std::fabs(rise.elevation) < 0.2 && rise.rangeRateKmS < 0 && top.rangeKm < rise.rangeKm);
        CHECK(top.rangeKm > 380 && rise.rangeKm < 2600);
        passes++;
        t = p.los + 60;
        if (t > s.epochUnix + 48 * 3600) break;
    }
    CHECK(passes >= 8 && passes <= 18);     // the ISS rises 4 to 8 times a day at this latitude
    CHECK(std::fabs(sgp4::dopplerHz(-7.0, 145.8) - 3404.0) < 5.0);     // approaching at 7 km/s: about +3.4 kHz on 2 m

    if (argc == 1) printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
