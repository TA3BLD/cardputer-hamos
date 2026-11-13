// "Where am I": live GPS position when there is a fix, otherwise the grid
// locator saved in Settings.
#pragma once
#include <string>
#include "Config.h"
#include "Gps.h"
#include "lib/Maidenhead.h"

namespace station {

inline std::string grid() {
    std::string g = gps::grid();
    return g.empty() ? config::get("grid") : g;
}

inline bool position(double& lat, double& lon) {
    if (gps::hasFix()) {
        lat = gps::fix().lat;
        lon = gps::fix().lon;
        return true;
    }
    return maidenhead::toLatLon(config::get("grid"), lat, lon);
}

}  // namespace station
