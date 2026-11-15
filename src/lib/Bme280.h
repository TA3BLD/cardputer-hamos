// Bosch BME280 temperature / humidity / pressure sensor: calibration and the
// compensation formulas from the datasheet (floating point version).
#pragma once
#include <cstdint>

namespace bme280 {

constexpr uint8_t kAddress = 0x76, kAddressAlt = 0x77, kChipId = 0x60;
constexpr uint8_t kRegCalib1 = 0x88, kRegCalib2 = 0xE1, kRegId = 0xD0, kRegCtrlHum = 0xF2, kRegCtrlMeas = 0xF4,
                  kRegConfig = 0xF5, kRegData = 0xF7;

struct Calibration {
    uint16_t t1 = 0;
    int16_t t2 = 0, t3 = 0;
    uint16_t p1 = 0;
    int16_t p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0;
    uint8_t h1 = 0, h3 = 0;
    int16_t h2 = 0, h4 = 0, h5 = 0;
    int8_t h6 = 0;
};

// c1: 26 bytes from 0x88, c2: 7 bytes from 0xE1
inline Calibration parseCalibration(const uint8_t* c1, const uint8_t* c2) {
    auto u16 = [](const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); };
    auto s16 = [](const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); };
    Calibration c;
    c.t1 = u16(c1 + 0);
    c.t2 = s16(c1 + 2);
    c.t3 = s16(c1 + 4);
    c.p1 = u16(c1 + 6);
    c.p2 = s16(c1 + 8);
    c.p3 = s16(c1 + 10);
    c.p4 = s16(c1 + 12);
    c.p5 = s16(c1 + 14);
    c.p6 = s16(c1 + 16);
    c.p7 = s16(c1 + 18);
    c.p8 = s16(c1 + 20);
    c.p9 = s16(c1 + 22);
    c.h1 = c1[25];                                          // 0xA1
    c.h2 = s16(c2 + 0);
    c.h3 = c2[2];
    int h4 = (c2[3] << 4) | (c2[4] & 0x0F);                // 12-bit signed values
    int h5 = (c2[5] << 4) | (c2[4] >> 4);
    c.h4 = (int16_t)(h4 & 0x800 ? h4 - 0x1000 : h4);
    c.h5 = (int16_t)(h5 & 0x800 ? h5 - 0x1000 : h5);
    c.h6 = (int8_t)c2[6];
    return c;
}

struct Reading {
    double celsius = 0, pascal = 0, humidity = 0;
};

// raw: 8 bytes from 0xF7 (pressure, temperature: 20 bits each; humidity 16 bits)
inline Reading compensate(const Calibration& c, const uint8_t* raw) {
    int32_t adcP = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    int32_t adcT = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);
    int32_t adcH = (raw[6] << 8) | raw[7];
    Reading r;
    double v1 = (adcT / 16384.0 - c.t1 / 1024.0) * c.t2;
    double v2 = adcT / 131072.0 - c.t1 / 8192.0;
    v2 = v2 * v2 * c.t3;
    double tFine = v1 + v2;
    r.celsius = tFine / 5120.0;

    v1 = tFine / 2.0 - 64000.0;
    v2 = v1 * v1 * c.p6 / 32768.0;
    v2 = v2 + v1 * c.p5 * 2.0;
    v2 = v2 / 4.0 + c.p4 * 65536.0;
    v1 = (c.p3 * v1 * v1 / 524288.0 + c.p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * c.p1;
    if (v1 != 0) {
        double p = 1048576.0 - adcP;
        p = (p - v2 / 4096.0) * 6250.0 / v1;
        v1 = c.p9 * p * p / 2147483648.0;
        v2 = p * c.p8 / 32768.0;
        r.pascal = p + (v1 + v2 + c.p7) / 16.0;
    }

    double h = tFine - 76800.0;
    h = (adcH - (c.h4 * 64.0 + c.h5 / 16384.0 * h)) *
        (c.h2 / 65536.0 * (1.0 + c.h6 / 67108864.0 * h * (1.0 + c.h3 / 67108864.0 * h)));
    h = h * (1.0 - c.h1 * h / 524288.0);
    r.humidity = h > 100 ? 100 : h < 0 ? 0 : h;
    return r;
}

}  // namespace bme280
