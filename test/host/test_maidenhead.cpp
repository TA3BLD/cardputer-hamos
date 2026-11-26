// Host-side test: g++ -std=c++17 -I src test/host/test_maidenhead.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include "lib/Maidenhead.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main() {
    using namespace maidenhead;
    CHECK(fromLatLon(38.42, 27.14) == "KM38nk");        // Izmir
    CHECK(fromLatLon(41.7148, -72.7272) == "FN31pr");   // W1AW
    CHECK(fromLatLon(-33.87, 151.21) == "QF56od");      // Sydney
    CHECK(isValid("KM38") && isValid("km38NK"));
    CHECK(!isValid("KM3") && !isValid("ZZ00") && !isValid("KM38zz") && !isValid("KMAA"));

    double lat, lon;
    CHECK(toLatLon("FN31pr", lat, lon));
    CHECK(std::fabs(lat - 41.7148) < 0.03 && std::fabs(lon + 72.7272) < 0.05);
    CHECK(fromLatLon(lat, lon) == "FN31pr");            // round trip
    CHECK(toLatLon("KM38", lat, lon) && std::fabs(lat - 38.5) < 1e-9 && std::fabs(lon - 27.0) < 1e-9);
    CHECK(!toLatLon("bad", lat, lon));

    // London -> Paris: ~343.5 km, ~148 degrees
    double d = distanceKm(51.5074, -0.1278, 48.8566, 2.3522);
    double b = bearingDeg(51.5074, -0.1278, 48.8566, 2.3522);
    CHECK(std::fabs(d - 343.5) < 1.5);
    CHECK(std::fabs(b - 148.1) < 1.0);
    CHECK(std::fabs(bearingDeg(0, 0, 0, -10) - 270.0) < 1e-6);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
