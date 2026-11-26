// Name, town and locator of a callsign from an online callbook: HamQTH (free
// account) or QRZ.com (XML subscription), whichever has a user name in Settings.
#pragma once
#include <string>

namespace callbook {

struct Info {
    std::string call, name, qth, grid, country;
};

bool configured();
const char* service();          // "HamQTH", "QRZ.com" or ""
// Blocking (a second or two); answers from memory for callsigns looked up before.
bool lookup(const std::string& call, Info& out, std::string& error);

}  // namespace callbook
