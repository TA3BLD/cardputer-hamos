// Parks on the Air (pota.app) and Summits on the Air (sota.org.uk): live
// spots, spotting yourself on POTA, and the parks / summits near you
// (downloaded once to the SD card, only those within "near_km").
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pota {

enum Program { POTA, SOTA };

struct Spot {
    std::string activator, ref, name, mode, comment, spotter, location;
    double freqKhz = 0;
    uint32_t unixTime = 0;
    double km = -1;             // from our position, -1 unknown
};
bool fetchSpots(Program p, std::vector<Spot>& out, std::string& error);

// POTA only: SOTA needs an account login that HamOS cannot do.
bool selfSpot(const std::string& ref, double freqKhz, const std::string& mode, const std::string& comment, std::string& error);

struct Place {
    std::string ref, name, info;    // info: region (POTA) or "1234 m, 8 pt" (SOTA)
    double lat = 0, lon = 0, km = 0, bearing = 0;
};
// Downloads the full list (several MB) and keeps the places within `radiusKm`.
bool downloadNearby(Program p, double radiusKm, void (*progress)(long done, long total), size_t& kept, std::string& error);
// The nearest places from the SD card file, nearest first.
std::vector<Place> nearby(Program p, size_t max, std::string& error);
const char* nearbyPath(Program p);

// Contacts logged today (UTC) from the reference in Settings (pota_ref / sota_ref).
int activationCount(Program p);

}  // namespace pota
