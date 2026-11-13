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
bool remove(const char* path);
bool rename(const char* from, const char* to);
// Streams a file in pieces so large files never have to fit in RAM.
typedef void (*ChunkHandler)(const char* data, size_t len, void* user);
bool readChunks(const char* path, ChunkHandler handler, void* user);
}
