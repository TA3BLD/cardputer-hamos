// M5Stack LoRa + GNSS cap for the Cardputer ADV (Cap LoRa868 / Cap LoRa-1262).
//
//   SX1262:  NSS G5, RST G3, DIO1 G4, BUSY G6, SPI shared with the microSD
//            (SCK G40, MISO G39, MOSI G14)
//   GNSS:    ATGM336H, 115200 baud, module TX -> G15, module RX <- G13
//   LoRa-1262 version only: PI4IOE5V6408 I/O expander at I2C 0x43 (SDA G8,
//            SCL G9); P0 = antenna RF switch, P1 = GNSS LNA enable.
#pragma once

namespace cap {

constexpr int LORA_CS = 5, LORA_RST = 3, LORA_DIO1 = 4, LORA_BUSY = 6;
constexpr int GPS_RX = 15, GPS_TX = 13;
constexpr long GPS_BAUD = 115200;
// The Grove port (HY2.0-4P): two free signal pins. Used for a CAT cable
// (G1 = from the radio, G2 = to the radio), or key / PTT outputs, or a paddle.
constexpr int GROVE_1 = 1, GROVE_2 = 2;

// Call first in setup(), before the SD card is initialised.
void begin();
bool hasExpander();

}  // namespace cap
