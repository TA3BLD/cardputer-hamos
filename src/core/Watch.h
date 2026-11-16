// Watch list (setting "watch"): when a watched call, prefix, park or a new
// country shows up in the DX cluster, POTA/SOTA spots or FT8 decodes, HamOS
// beeps and says so in the status bar, whatever app is open. POTA spots are
// checked in the background every 3 minutes while WiFi is up.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace watch {

struct Hit {
    std::string call, where, info;      // where: "cluster", "POTA", "SOTA", "FT8"
    double khz = 0;
    uint32_t ms = 0;
};

// A station heard or spotted; `ref` is a park / summit reference if any.
void check(const std::string& call, double khz, const std::string& ref, const char* where, const std::string& info);
const std::vector<Hit>& hits();
std::string takeNotice();           // "" or the newest hit to announce
void tick(bool idle);

}  // namespace watch
