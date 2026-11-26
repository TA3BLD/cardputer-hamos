// GNSS receiver on the LoRa cap. Non-blocking: call tick() from the main loop.
#pragma once
#include <string>
#include "lib/Nmea.h"

namespace gps {
void begin();
void tick();
bool present();                 // a receiver is sending data
bool hasFix();                  // current, valid position
const nmea::Fix& fix();
std::string grid();             // 6 character locator, "" without a fix
}
