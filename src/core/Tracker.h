// Tracker: position beacons while walking or driving, timed by SmartBeaconing,
// over LoRa-APRS (or APRS-IS) or as Meshtastic positions. The screen can stay
// dark meanwhile to save the battery.
#pragma once
#include <cstdint>
#include <string>

namespace tracker {

void start();
void stop();
bool running();
void tick();
uint32_t sent();
double secondsToNext();     // estimate at the current speed, -1 without a fix
std::string via();          // "LoRa-APRS", "APRS-IS", "mesh" or why nothing can send

}  // namespace tracker
