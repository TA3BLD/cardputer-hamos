// Everyday amateur radio formulas. Pure C++, host-testable.
#pragma once
#include <cmath>

namespace radiomath {

constexpr double kPi = 3.14159265358979323846;

// Antenna lengths in metres for a frequency in MHz (wire velocity factor 0.95).
inline double wavelengthM(double mhz) { return 299.792458 / mhz; }
inline double halfWaveDipoleM(double mhz) { return 142.5 / mhz; }
inline double quarterWaveM(double mhz) { return 71.25 / mhz; }

// SWR from forward and reflected power (same unit). Returns infinity if all is reflected.
inline double swr(double fwd, double ref) {
    if (fwd <= 0 || ref < 0) return NAN;
    double rho = std::sqrt(ref / fwd);
    return rho >= 1.0 ? INFINITY : (1.0 + rho) / (1.0 - rho);
}
inline double returnLossDb(double fwd, double ref) { return ref <= 0 ? INFINITY : 10.0 * std::log10(fwd / ref); }

inline double wattsToDbm(double w) { return 10.0 * std::log10(w * 1000.0); }
inline double dbmToWatts(double dbm) { return std::pow(10.0, dbm / 10.0) / 1000.0; }

// Power remaining after a loss in dB.
inline double afterLossW(double watts, double lossDb) { return watts * std::pow(10.0, -lossDb / 10.0); }

// Resonant frequency in MHz of L (microhenry) and C (picofarad).
inline double resonanceMhz(double uH, double pF) {
    return 1.0 / (2.0 * kPi * std::sqrt(uH * 1e-6 * pF * 1e-12)) / 1e6;
}

}  // namespace radiomath
