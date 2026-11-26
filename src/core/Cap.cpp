#include "Cap.h"
#include <M5Cardputer.h>

namespace cap {

static bool expander = false;

void begin() {
    // Keep the radio off the shared SPI bus while the SD card starts up.
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);

    // Newer caps route the antenna switch and the GNSS amplifier through an
    // I/O expander. Older caps have none; then these writes simply go nowhere.
    const uint8_t addr = 0x43, mask = 0x03;
    const uint32_t freq = 400000;
    expander = M5.In_I2C.scanID(addr, freq);
    if (!expander) return;
    M5.In_I2C.writeRegister8(addr, 0x03, M5.In_I2C.readRegister8(addr, 0x03, freq) | mask, freq);    // outputs
    M5.In_I2C.writeRegister8(addr, 0x05, M5.In_I2C.readRegister8(addr, 0x05, freq) | mask, freq);    // drive high
    M5.In_I2C.writeRegister8(addr, 0x07, M5.In_I2C.readRegister8(addr, 0x07, freq) & ~mask, freq);   // not high-Z
}

bool hasExpander() { return expander; }

}  // namespace cap
