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


// The point where the sun is overhead at `unixSec` (UTC), for the day/night line.
inline void subsolar(double unixSec, double& lat, double& lon) {
    const double pi = 3.14159265358979323846;
    double days = unixSec / 86400.0;
    double dayOfYear = fmod(days - 10957.0, 365.2422);          // days since 2000-01-01, within the year
    if (dayOfYear < 0) dayOfYear += 365.2422;
    double hours = fmod(unixSec, 86400.0) / 3600.0;
    double g = 2.0 * pi / 365.0 * (dayOfYear - 1 + (hours - 12) / 24);
    double eqTime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g) -
                  0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
    lat = decl * 180.0 / pi;
    lon = -15.0 * (hours - 12.0 + eqTime / 60.0);
    while (lon < -180) lon += 360;
    while (lon > 180) lon -= 360;
}

// Latitude of the day/night line at longitude `lon`, for a subsolar point.
inline double terminatorLat(double lon, double sunLat, double sunLon) {
    const double d2r = 3.14159265358979323846 / 180.0;
    double t = tan(sunLat * d2r);
    if (fabs(t) < 1e-6) t = 1e-6;
    return atan(-cos((lon - sunLon) * d2r) / t) / d2r;
}

}  // namespace sun
