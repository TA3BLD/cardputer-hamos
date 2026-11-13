// Sunrise and sunset (NOAA approximation, accurate to a few minutes).
// Pure C++, host-testable.
#pragma once
#include <cmath>

namespace sun {

struct Times {
    bool rises = false;         // false during polar day / polar night
    double sunriseUtc = 0;      // hours 0..24, may wrap past midnight UTC
    double sunsetUtc = 0;
};

inline int dayOfYear(int year, int month, int day) {
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return cum[month - 1] + day + (leap && month > 2 ? 1 : 0);
}

inline Times compute(int year, int month, int day, double lat, double lon) {
    const double pi = 3.14159265358979323846, d2r = pi / 180.0;
    double g = 2.0 * pi / 365.0 * (dayOfYear(year, month, day) - 1);   // fractional year
    double eqTime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) -
                              0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) +
                  0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
    double cosH = cos(90.833 * d2r) / (cos(lat * d2r) * cos(decl)) - tan(lat * d2r) * tan(decl);
    Times t;
    if (cosH < -1.0 || cosH > 1.0) return t;
    double h = acos(cosH) / d2r;                                       // degrees
    double noonMin = 720.0 - 4.0 * lon - eqTime;
    t.rises = true;
    t.sunriseUtc = fmod((noonMin - 4.0 * h) / 60.0 + 48.0, 24.0);
    t.sunsetUtc = fmod((noonMin + 4.0 * h) / 60.0 + 48.0, 24.0);
    return t;
}

}  // namespace sun
