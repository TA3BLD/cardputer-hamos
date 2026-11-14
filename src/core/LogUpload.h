// Sending the logbook to online logs: eQSL, Club Log and the QRZ.com logbook,
// each with the account details from Settings; and a Cabrillo file for contests.
//
// For each service HamOS remembers the time of the newest contact sent, so
// "upload" sends only what is new. The services recognise duplicates, so
// sending everything again ("all") is harmless.
#pragma once
#include <string>

namespace logupload {

enum Service { EQSL, CLUBLOG, QRZ, COUNT };

const char* name(Service s);
bool configured(Service s);
const char* missingSetting(Service s);      // which setting to fill in, "" when configured

struct Counts {
    size_t waiting[COUNT] = {};
    size_t total = 0;
};
Counts count();                             // reads the whole log

struct Result {
    int sent = 0, dupes = 0, failed = 0;
    std::string error;                      // why it stopped, "" if it did not
};
// Blocking: one request per contact. `progress` (optional) after each one.
Result upload(Service s, bool all, void (*progress)(int done, int total));

// Writes /HamOS/contest.cbr from the contacts of the last `hours` (0: all).
bool exportCabrillo(int hours, size_t& contacts, std::string& error);
constexpr const char* kCabrilloPath = "/HamOS/contest.cbr";

}  // namespace logupload
