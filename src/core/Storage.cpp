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

}  // namespace storage
