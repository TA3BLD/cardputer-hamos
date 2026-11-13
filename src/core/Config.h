// Settings stored as key=value lines in /HamOS/config.txt on the SD card.
#pragma once
#include <string>

namespace config {

void load();                 // call after storage::begin()
bool save();                 // false if the SD card is unavailable
std::string get(const std::string& key, const std::string& def = "");
void set(const std::string& key, const std::string& value);

// Pure helpers (no SD access), exposed for tests.
void parse(const std::string& text);
std::string serialize();

}  // namespace config
