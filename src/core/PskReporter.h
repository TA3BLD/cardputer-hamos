// PSK Reporter (pskreporter.info): report the stations HamOS decodes
// ("psk_report" = 1), and ask who heard our own signal.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pskr {

// A decoded station. Sent in batches every five minutes; the same station on
// the same band is reported at most once in half an hour.
void add(const std::string& call, const std::string& locator, const char* mode, uint32_t freqHz, int snr);
void tick();
size_t queued();
size_t sentTotal();

struct Heard {
    std::string call, locator, mode, country;
    uint32_t freqHz = 0;
    int snr = 0;
    uint32_t unixTime = 0;
    double km = -1, bearing = 0;   // from our position, -1 unknown
};
// Who reported our callsign in the last `minutes` (newest first). PSK
// Reporter asks for no more than one query every five minutes; asking again
// sooner returns the last answer.
bool heardMe(std::vector<Heard>& out, std::string& error, int minutes = 60);

}  // namespace pskr
