// Maidenhead locator maths. Pure C++ (no Arduino dependency) so it can be
// unit-tested on a PC: see test/host/test_maidenhead.cpp
#pragma once
#include <cctype>
#include <cmath>
#include <string>

namespace maidenhead {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusKm = 6371.0;

// Accepts 4 or 6 character locators, any letter case ("KM38", "km38nk").
inline bool isValid(const std::string& g) {
    if (g.size() != 4 && g.size() != 6) return false;
    for (int i = 0; i < 2; i++) {
        char c = (char)toupper((unsigned char)g[i]);
        if (c < 'A' || c > 'R') return false;
    }
    for (int i = 2; i < 4; i++)
        if (!isdigit((unsigned char)g[i])) return false;
    for (size_t i = 4; i < g.size(); i++) {
        char c = (char)toupper((unsigned char)g[i]);
        if (c < 'A' || c > 'X') return false;
    }
    return true;
}

// Latitude/longitude in degrees (north/east positive) -> 6 character locator.
inline std::string fromLatLon(double lat, double lon) {
    lon = std::fmin(std::fmax(lon + 180.0, 0.0), 359.999999);
    lat = std::fmin(std::fmax(lat + 90.0, 0.0), 179.999999);
    std::string g(6, ' ');
    g[0] = (char)('A' + (int)(lon / 20.0));
    g[1] = (char)('A' + (int)(lat / 10.0));
    lon = std::fmod(lon, 20.0);
    lat = std::fmod(lat, 10.0);
    g[2] = (char)('0' + (int)(lon / 2.0));
    g[3] = (char)('0' + (int)lat);
    lon = std::fmod(lon, 2.0);
    lat = std::fmod(lat, 1.0);
    g[4] = (char)('a' + (int)(lon * 12.0));
    g[5] = (char)('a' + (int)(lat * 24.0));
    return g;
}

// Locator -> centre of the square. Returns false if the locator is invalid.
inline bool toLatLon(const std::string& g, double& lat, double& lon) {
    if (!isValid(g)) return false;
    lon = (toupper((unsigned char)g[0]) - 'A') * 20.0 + (g[2] - '0') * 2.0;
    lat = (toupper((unsigned char)g[1]) - 'A') * 10.0 + (g[3] - '0') * 1.0;
    if (g.size() == 6) {
        lon += (toupper((unsigned char)g[4]) - 'A') / 12.0 + 1.0 / 24.0;
        lat += (toupper((unsigned char)g[5]) - 'A') / 24.0 + 1.0 / 48.0;
    } else {
        lon += 1.0;
        lat += 0.5;
    }
    lon -= 180.0;
    lat -= 90.0;
    return true;
}

inline double rad(double deg) { return deg * kPi / 180.0; }

// Great-circle distance in km (haversine).
inline double distanceKm(double lat1, double lon1, double lat2, double lon2) {
    double dLat = rad(lat2 - lat1), dLon = rad(lon2 - lon1);
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusKm * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// Initial (short path) bearing from point 1 to point 2, 0..360 degrees.
inline double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double dLon = rad(lon2 - lon1);
    double y = std::sin(dLon) * std::cos(rad(lat2));
    double x = std::cos(rad(lat1)) * std::sin(rad(lat2)) -
               std::sin(rad(lat1)) * std::cos(rad(lat2)) * std::cos(dLon);
    double b = std::atan2(y, x) * 180.0 / kPi;
    return std::fmod(b + 360.0, 360.0);
}

}  // namespace maidenhead
