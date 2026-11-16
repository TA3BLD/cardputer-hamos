// SmartBeaconing (HamHUD): beacon often when moving fast, rarely when slow or
// parked, and at once on a corner. Speeds in km/h, times in seconds.
#pragma once
#include <cmath>

namespace smartbeacon {

struct Params {
    double fastSpeed = 90, fastRate = 60;       // at or above fastSpeed: every fastRate s
    double slowSpeed = 5, slowRate = 1800;      // below slowSpeed: every slowRate s
    double minTurnTime = 15, minTurnAngle = 28, turnSlope = 255;
};

// Seconds between beacons at this speed.
inline double rate(const Params& p, double speed) {
    if (speed < p.slowSpeed) return p.slowRate;
    if (speed >= p.fastSpeed) return p.fastRate;
    return p.fastRate * p.fastSpeed / speed;
}

// Is a beacon due now? `since`: seconds since the last one; courses in degrees.
inline bool due(const Params& p, double since, double speed, double course, double lastCourse) {
    if (since >= rate(p, speed)) return true;
    if (speed < p.slowSpeed || since < p.minTurnTime) return false;
    double turn = std::fabs(course - lastCourse);
    if (turn > 180) turn = 360 - turn;
    return turn > p.minTurnAngle + p.turnSlope / speed;
}

}  // namespace smartbeacon
