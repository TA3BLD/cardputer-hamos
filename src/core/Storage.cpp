#include "Storage.h"
#include <SD.h>
#include <SPI.h>

namespace storage {

// Cardputer / Cardputer ADV microSD wiring
static const int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;
static bool isReady = false;

bool begin() {
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    for (int attempt = 0; attempt < 3 && !isReady; attempt++) {
        isReady = SD.begin(PIN_CS, SPI, 25000000);
        if (!isReady) delay(200);
    }
    if (isReady && !SD.exists("/HamOS")) SD.mkdir("/HamOS");
    return isReady;
}

bool ready() { return isReady; }

bool exists(const char* path) { return isReady && SD.exists(path); }

bool readFile(const char* path, std::string& out) {
    out.clear();
    if (!isReady) return false;
    File f = SD.open(path);
    if (!f) return false;
    uint8_t buf[256];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        out.append((const char*)buf, (size_t)n);
    }
    f.close();
    return true;
}

static bool put(const char* path, const std::string& text, const char* mode) {
    if (!isReady) return false;
    File f = SD.open(path, mode);
    if (!f) return false;
    size_t written = f.write((const uint8_t*)text.data(), text.size());
    f.close();
    return written == text.size();
}

bool writeFile(const char* path, const std::string& text) {
    if (isReady) SD.remove(path);
    return put(path, text, FILE_WRITE);
}

bool appendFile(const char* path, const std::string& text) { return put(path, text, FILE_APPEND); }

}  // namespace storage
