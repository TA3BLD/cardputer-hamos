// SD card access. HamOS still boots without a card; data just doesn't persist.
#pragma once
#include <string>

namespace storage {
bool begin();
bool ready();
bool readFile(const char* path, std::string& out);
bool writeFile(const char* path, const std::string& text);    // replaces the file
bool appendFile(const char* path, const std::string& text);   // creates it if missing
bool exists(const char* path);
}
