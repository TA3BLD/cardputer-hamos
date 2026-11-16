// TwoWire signatures from arduino-esp32 2.0.17 (subset)
#pragma once
#include <cstdint>
#include <cstddef>
class TwoWire {
public:
    bool begin(int sda, int scl, uint32_t frequency = 0);
    void beginTransmission(uint16_t address);
    uint8_t endTransmission(bool sendStop);
    uint8_t endTransmission(void);
    uint8_t requestFrom(int address, int size);
    size_t write(uint8_t);
    int read(void);
};
extern TwoWire Wire1;
