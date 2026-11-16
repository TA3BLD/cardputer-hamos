// Band openings: every 15 minutes (setting "band_alert" = 1, WiFi, nothing
// busy on screen) HamOS asks PSK Reporter which stations in your grid square
// heard, or were heard by, stations more than 2500 km away in the last 15
// minutes. A band with several such paths counts as open; a band that opens
// is announced in the status bar.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace openings {

struct Band {
    std::string name;                   // "20m"
    int paths = 0;                      // distinct far stations
    std::set<std::string> continents;   // where they are: EU AS AF NA SA OC
    double farthestKm = 0;
};

void tick(bool idle);
bool check(std::string& error);         // asks now (a few seconds)
const std::vector<Band>& bands();       // open bands, most paths first
uint32_t lastCheckMs();
std::string takeNotice();               // "20m open: NA SA" when a band newly opened

// For the tests: count one report.
void addReport(std::map<std::string, Band>& acc, std::set<std::string>& seen, const std::string& farCall, const std::string& farLocator,
               uint32_t freqHz, double myLat, double myLon);

}  // namespace openings
