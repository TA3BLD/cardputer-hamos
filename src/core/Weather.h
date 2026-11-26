// Weather station: a BME280 sensor on the Grove port (Settings: Grove = wx;
// G2 = SDA, G1 = SCL, 3.3 V, GND) read once a minute, and APRS weather
// reports over LoRa-APRS / APRS-IS every "WX beacon min" minutes.
#pragma once
#include <cstdint>
#include <string>

namespace weather {

void begin();
void tick();
bool present();                     // the sensor answered
bool valid();
double celsius();
double humidity();
double pressureHpa();               // at the station
double seaLevelHpa();               // reduced with the altitude (GPS or "WX altitude m")
std::string status();
bool beaconNow(std::string& error);
uint32_t lastBeaconMs();

}  // namespace weather
