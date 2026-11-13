// SX1262 LoRa radio on the cap. Transmit and receive are non-blocking: call
// tick() from the main loop.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lora {

struct Settings {
    double freqMhz = 869.525, bwKhz = 250;
    int sf = 11, cr = 5;
    uint8_t syncWord = 0x12;
    int preamble = 8;
    int powerDbm = 14;
};

typedef void (*RxHandler)(const uint8_t* data, size_t len, float rssi, float snr);

bool begin(const Settings& s);      // false if no radio answers; see error()
bool ready();
std::string error();                // "" when ready
void onReceive(RxHandler handler);
bool send(const uint8_t* data, size_t len);   // false if not ready or still transmitting
bool busy();                        // a transmission is in progress
void tick();

}  // namespace lora
