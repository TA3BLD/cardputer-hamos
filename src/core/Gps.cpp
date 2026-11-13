#include "Gps.h"
#include <Arduino.h>
#include <sys/time.h>
#include "Cap.h"
#include "Net.h"
#include "lib/Maidenhead.h"

namespace gps {

static nmea::Parser parser;
static uint32_t lastSentenceMs = 0, lastFixMs = 0, lastClockSetMs = 0;
static bool clockSet = false;

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void setSystemClock(const nmea::Fix& f) {
    time_t t = (time_t)daysFromCivil(f.year, f.month, f.day) * 86400 + f.hour * 3600 + f.minute * 60 + f.second;
#ifndef HAMOS_SIM
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
#else
    (void)t;
#endif
}

void begin() { Serial1.begin(cap::GPS_BAUD, SERIAL_8N1, cap::GPS_RX, cap::GPS_TX); }

void tick() {
    uint32_t now = millis();
    for (int budget = 512; budget > 0 && Serial1.available(); budget--) {
        if (!parser.feed((char)Serial1.read())) continue;
        lastSentenceMs = now;
        if (parser.fix.valid) lastFixMs = now;
        // GPS sets the clock when there is no internet time; refreshed every 10 minutes.
        bool due = !clockSet || now - lastClockSetMs > 600000;
        if (parser.fix.timeValid && due && !(net::connected() && net::timeValid())) {
            setSystemClock(parser.fix);
            clockSet = true;
            lastClockSetMs = now;
        }
    }
}

bool present() { return parser.sentences > 0 && millis() - lastSentenceMs < 5000; }
bool hasFix() { return parser.fix.valid && lastFixMs != 0 && millis() - lastFixMs < 5000; }
const nmea::Fix& fix() { return parser.fix; }
std::string grid() { return hasFix() ? maidenhead::fromLatLon(parser.fix.lat, parser.fix.lon) : ""; }

}  // namespace gps
