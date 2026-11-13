// The station logbook: a standard ADIF file on the SD card (/HamOS/log.adi).
// The file is always read and rewritten as a stream, so its size is not
// limited by RAM; only the newest contacts are kept in memory for display.
#pragma once
#include <string>
#include <vector>
#include "lib/Adif.h"
#include "lib/Dxcc.h"

namespace logbook {

constexpr size_t MAX_RECENT = 200;
constexpr const char* kPath = "/HamOS/log.adi";

void load();                                    // reads the file once; later calls do nothing
const std::vector<adif::Qso>& recent();         // oldest first, at most MAX_RECENT
size_t total();                                 // contacts in the file
bool add(const adif::Qso& q);                   // false if it could not be written to the SD card
bool remove(size_t indexInRecent);              // deletes one contact from the file
bool workedEntity(const dxcc::Entity* e);       // any contact with this country, whole log
// Earlier contacts with a callsign among the recent ones; `dupes` = same band and mode.
int worked(const std::string& call, const std::string& freq, const std::string& mode, int& dupes);

}  // namespace logbook
