// Satellite orbit prediction: SGP4 (near-earth part, for low orbits such as
// the ISS and amateur satellites), look angles and pass search.
// After Vallado et al., "Revisiting Spacetrack Report #3". Pure C++, host-testable.
#pragma once
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>

namespace sgp4 {

constexpr double kPi = 3.14159265358979323846, kTwoPi = 2 * kPi, kDeg = kPi / 180.0;
constexpr double kEarthKm = 6378.135, kMu = 398600.8, kJ2 = 0.001082616, kJ3 = -0.00000253881, kJ4 = -0.00000165597;
constexpr double kTwoThirds = 2.0 / 3.0;

struct Satellite {
    bool valid = false;
    std::string name;
    double epochUnix = 0;       // seconds since 1970
    // elements
    double bstar = 0, inclo = 0, nodeo = 0, ecco = 0, argpo = 0, mo = 0, no = 0;
    // derived constants
    int isimp = 0;
    double aycof, con41, cc1, cc4, cc5, d2, d3, d4, delmo, eta, argpdot, omgcof, sinmao, t2cof, t3cof, t4cof, t5cof,
        x1mth2, x7thm1, mdot, nodedot, xlcof, xmcof, nodecf, xke;
};

inline double field(const std::string& line, size_t col, size_t len) {   // 1-based column
    return line.size() >= col - 1 + len ? atof(line.substr(col - 1, len).c_str()) : 0;
}

// Days since 1970-01-01 of a civil date.
inline long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

// Builds a satellite from the two element lines. Returns an invalid satellite
// for malformed lines or orbits this model does not cover (period over 225 min).
inline Satellite fromTle(const std::string& name, const std::string& l1, const std::string& l2) {
    Satellite s;
    s.name = name;
    while (!s.name.empty() && (s.name.back() == ' ' || s.name.back() == '\r')) s.name.pop_back();
    if (l1.size() < 68 || l2.size() < 68 || l1[0] != '1' || l2[0] != '2') return s;

    int yy = (int)field(l1, 19, 2);
    double dayOfYear = field(l1, 21, 12);
    s.epochUnix = (daysFromCivil(yy < 57 ? 2000 + yy : 1900 + yy, 1, 1) + dayOfYear - 1.0) * 86400.0;
    // B* is written as "±12345-6" meaning ±0.12345e-6
    double mantissa = atof((l1.substr(53, 1) + "0." + l1.substr(54, 5)).c_str());
    s.bstar = mantissa * std::pow(10.0, atof(l1.substr(59, 2).c_str()));
    s.inclo = field(l2, 9, 8) * kDeg;
    s.nodeo = field(l2, 18, 8) * kDeg;
    s.ecco = atof(("0." + l2.substr(26, 7)).c_str());
    s.argpo = field(l2, 35, 8) * kDeg;
    s.mo = field(l2, 44, 8) * kDeg;
    double noKozai = field(l2, 53, 11) * kTwoPi / 1440.0;         // rad/min
    if (noKozai <= 0 || s.ecco >= 1 || kTwoPi / noKozai >= 225.0) return s;

    s.xke = 60.0 / std::sqrt(kEarthKm * kEarthKm * kEarthKm / kMu);
    const double ss = 78.0 / kEarthKm + 1.0, qzms2t = std::pow((120.0 - 78.0) / kEarthKm, 4), j3oj2 = kJ3 / kJ2;
    double eccsq = s.ecco * s.ecco, omeosq = 1.0 - eccsq, rteosq = std::sqrt(omeosq);
    double cosio = std::cos(s.inclo), cosio2 = cosio * cosio, sinio = std::sin(s.inclo);
    double ak = std::pow(s.xke / noKozai, kTwoThirds);
    double d1 = 0.75 * kJ2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
    double del = d1 / (ak * ak);
    double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del = d1 / (adel * adel);
    s.no = noKozai / (1.0 + del);
    double ao = std::pow(s.xke / s.no, kTwoThirds);
    double po = ao * omeosq, con42 = 1.0 - 5.0 * cosio2;
    s.con41 = -con42 - cosio2 - cosio2;
    double posq = po * po, rp = ao * (1.0 - s.ecco);

    s.isimp = rp < 220.0 / kEarthKm + 1.0 ? 1 : 0;
    double sfour = ss, qzms24 = qzms2t, perige = (rp - 1.0) * kEarthKm;
    if (perige < 156.0) {
        sfour = perige - 78.0;
        if (perige < 98.0) sfour = 20.0;
        qzms24 = std::pow((120.0 - sfour) / kEarthKm, 4);
        sfour = sfour / kEarthKm + 1.0;
    }
    double pinvsq = 1.0 / posq, tsi = 1.0 / (ao - sfour);
    s.eta = ao * s.ecco * tsi;
    double etasq = s.eta * s.eta, eeta = s.ecco * s.eta, psisq = std::fabs(1.0 - etasq);
    double coef = qzms24 * std::pow(tsi, 4), coef1 = coef / std::pow(psisq, 3.5);
    double cc2 = coef1 * s.no * (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                                 0.375 * kJ2 * tsi / psisq * s.con41 * (8.0 + 3.0 * etasq * (8.0 + etasq)));
    s.cc1 = s.bstar * cc2;
    double cc3 = s.ecco > 1.0e-4 ? -2.0 * coef * tsi * j3oj2 * s.no * sinio / s.ecco : 0.0;
    s.x1mth2 = 1.0 - cosio2;
    s.cc4 = 2.0 * s.no * coef1 * ao * omeosq *
            (s.eta * (2.0 + 0.5 * etasq) + s.ecco * (0.5 + 2.0 * etasq) -
             kJ2 * tsi / (ao * psisq) * (-3.0 * s.con41 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
                                        0.75 * s.x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) * std::cos(2.0 * s.argpo)));
    s.cc5 = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
    double cosio4 = cosio2 * cosio2;
    double temp1 = 1.5 * kJ2 * pinvsq * s.no, temp2 = 0.5 * temp1 * kJ2 * pinvsq, temp3 = -0.46875 * kJ4 * pinvsq * pinvsq * s.no;
    s.mdot = s.no + 0.5 * temp1 * rteosq * s.con41 + 0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
    s.argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 * (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
                temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
    double xhdot1 = -temp1 * cosio;
    s.nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) + 2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
    s.omgcof = s.bstar * cc3 * std::cos(s.argpo);
    s.xmcof = s.ecco > 1.0e-4 ? -kTwoThirds * coef * s.bstar / eeta : 0.0;
    s.nodecf = 3.5 * omeosq * xhdot1 * s.cc1;
    s.t2cof = 1.5 * s.cc1;
    double denom = std::fabs(cosio + 1.0) > 1.5e-12 ? 1.0 + cosio : 1.5e-12;
    s.xlcof = -0.25 * j3oj2 * sinio * (3.0 + 5.0 * cosio) / denom;
    s.aycof = -0.5 * j3oj2 * sinio;
    s.delmo = std::pow(1.0 + s.eta * std::cos(s.mo), 3);
    s.sinmao = std::sin(s.mo);
    s.x7thm1 = 7.0 * cosio2 - 1.0;
    s.d2 = s.d3 = s.d4 = s.t3cof = s.t4cof = s.t5cof = 0;
    if (!s.isimp) {
        double cc1sq = s.cc1 * s.cc1;
        s.d2 = 4.0 * ao * tsi * cc1sq;
        double temp = s.d2 * tsi * s.cc1 / 3.0;
        s.d3 = (17.0 * ao + sfour) * temp;
        s.d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * s.cc1;
        s.t3cof = s.d2 + 2.0 * cc1sq;
        s.t4cof = 0.25 * (3.0 * s.d3 + s.cc1 * (12.0 * s.d2 + 10.0 * cc1sq));
        s.t5cof = 0.2 * (3.0 * s.d4 + 12.0 * s.cc1 * s.d3 + 6.0 * s.d2 * s.d2 + 15.0 * cc1sq * (2.0 * s.d2 + cc1sq));
    }
    s.valid = true;
    return s;
}

// Position in km (TEME frame) `tsince` minutes after the element epoch.
inline bool propagate(const Satellite& s, double tsince, double r[3]) {
    double t = tsince, t2 = t * t;
    double xmdf = s.mo + s.mdot * t, argpdf = s.argpo + s.argpdot * t, nodedf = s.nodeo + s.nodedot * t;
    double argpm = argpdf, mm = xmdf, nodem = nodedf + s.nodecf * t2;
    double tempa = 1.0 - s.cc1 * t, tempe = s.bstar * s.cc4 * t, templ = s.t2cof * t2;
    if (!s.isimp) {
        double delomg = s.omgcof * t;
        double delm = s.xmcof * (std::pow(1.0 + s.eta * std::cos(xmdf), 3) - s.delmo);
        double temp = delomg + delm;
        mm = xmdf + temp;
        argpm = argpdf - temp;
        double t3 = t2 * t, t4 = t3 * t;
        tempa = tempa - s.d2 * t2 - s.d3 * t3 - s.d4 * t4;
        tempe = tempe + s.bstar * s.cc5 * (std::sin(mm) - s.sinmao);
        templ = templ + s.t3cof * t3 + t4 * (s.t4cof + t * s.t5cof);
    }
    double am = std::pow(s.xke / s.no, kTwoThirds) * tempa * tempa;
    double em = s.ecco - tempe;
    if (em >= 1.0 || em < -0.001 || am < 0.95) return false;     // decayed or elements too old
    if (em < 1.0e-6) em = 1.0e-6;
    mm = mm + s.no * templ;
    double xlm = mm + argpm + nodem;
    nodem = std::fmod(nodem, kTwoPi);
    argpm = std::fmod(argpm, kTwoPi);
    xlm = std::fmod(xlm, kTwoPi);
    mm = std::fmod(xlm - argpm - nodem, kTwoPi);

    double sinip = std::sin(s.inclo), cosip = std::cos(s.inclo);
    double axnl = em * std::cos(argpm);
    double temp = 1.0 / (am * (1.0 - em * em));
    double aynl = em * std::sin(argpm) + temp * s.aycof;
    double xl = mm + argpm + nodem + temp * s.xlcof * axnl;
    double u = std::fmod(xl - nodem, kTwoPi), eo1 = u, tem5 = 9999.9, sineo1 = 0, coseo1 = 0;
    for (int k = 0; k < 10 && std::fabs(tem5) >= 1.0e-12; k++) {
        sineo1 = std::sin(eo1);
        coseo1 = std::cos(eo1);
        tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
        if (std::fabs(tem5) >= 0.95) tem5 = tem5 > 0.0 ? 0.95 : -0.95;
        eo1 += tem5;
    }
    double ecose = axnl * coseo1 + aynl * sineo1, esine = axnl * sineo1 - aynl * coseo1;
    double el2 = axnl * axnl + aynl * aynl, pl = am * (1.0 - el2);
    if (pl < 0.0) return false;
    double rl = am * (1.0 - ecose), betal = std::sqrt(1.0 - el2);
    temp = esine / (1.0 + betal);
    double sinu = am / rl * (sineo1 - aynl - axnl * temp), cosu = am / rl * (coseo1 - axnl + aynl * temp);
    double su = std::atan2(sinu, cosu), sin2u = (cosu + cosu) * sinu, cos2u = 1.0 - 2.0 * sinu * sinu;
    temp = 1.0 / pl;
    double temp1 = 0.5 * kJ2 * temp, temp2 = temp1 * temp;
    double mrt = rl * (1.0 - 1.5 * temp2 * betal * s.con41) + 0.5 * temp1 * s.x1mth2 * cos2u;
    su = su - 0.25 * temp2 * s.x7thm1 * sin2u;
    double xnode = nodem + 1.5 * temp2 * cosip * sin2u;
    double xinc = s.inclo + 1.5 * temp2 * cosip * sinip * cos2u;
    double sinsu = std::sin(su), cossu = std::cos(su), snod = std::sin(xnode), cnod = std::cos(xnode);
    double sini = std::sin(xinc), cosi = std::cos(xinc);
    double xmx = -snod * cosi, xmy = cnod * cosi;
    r[0] = mrt * (xmx * sinsu + cnod * cossu) * kEarthKm;
    r[1] = mrt * (xmy * sinsu + snod * cossu) * kEarthKm;
    r[2] = mrt * (sini * sinsu) * kEarthKm;
    return mrt >= 1.0;
}

struct Look {
    bool valid = false;
    double azimuth = 0, elevation = 0;  // degrees
    double rangeKm = 0;
    double rangeRateKmS = 0;            // positive = moving away
    double subLat = 0, subLon = 0;      // point on the ground below the satellite
};

inline double gmst(double unixSeconds) {
    double jd = unixSeconds / 86400.0 + 2440587.5, t = (jd - 2451545.0) / 36525.0;
    double sec = -6.2e-6 * t * t * t + 0.093104 * t * t + (876600.0 * 3600.0 + 8640184.812866) * t + 67310.54841;
    double rad = std::fmod(sec * kTwoPi / 86400.0, kTwoPi);
    return rad < 0 ? rad + kTwoPi : rad;
}

inline bool rangeAt(const Satellite& s, double unixSeconds, double latDeg, double lonDeg, double altM, double topo[3],
                    double ecef[3]) {
    double r[3];
    if (!s.valid || !propagate(s, (unixSeconds - s.epochUnix) / 60.0, r)) return false;
    double g = gmst(unixSeconds), cg = std::cos(g), sg = std::sin(g);
    ecef[0] = cg * r[0] + sg * r[1];
    ecef[1] = -sg * r[0] + cg * r[1];
    ecef[2] = r[2];
    // Observer on the WGS-84 ellipsoid.
    const double a = 6378.137, f = 1.0 / 298.257223563, e2 = f * (2 - f);
    double lat = latDeg * kDeg, lon = lonDeg * kDeg, h = altM / 1000.0;
    double n = a / std::sqrt(1 - e2 * std::sin(lat) * std::sin(lat));
    double ox = (n + h) * std::cos(lat) * std::cos(lon), oy = (n + h) * std::cos(lat) * std::sin(lon),
           oz = (n * (1 - e2) + h) * std::sin(lat);
    double dx = ecef[0] - ox, dy = ecef[1] - oy, dz = ecef[2] - oz;
    // South / east / up
    topo[0] = std::sin(lat) * std::cos(lon) * dx + std::sin(lat) * std::sin(lon) * dy - std::cos(lat) * dz;
    topo[1] = -std::sin(lon) * dx + std::cos(lon) * dy;
    topo[2] = std::cos(lat) * std::cos(lon) * dx + std::cos(lat) * std::sin(lon) * dy + std::sin(lat) * dz;
    return true;
}

inline Look look(const Satellite& s, double unixSeconds, double latDeg, double lonDeg, double altM = 0) {
    Look l;
    double t[3], e[3], t2[3], e2[3];
    if (!rangeAt(s, unixSeconds, latDeg, lonDeg, altM, t, e)) return l;
    l.rangeKm = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    l.elevation = std::asin(t[2] / l.rangeKm) / kDeg;
    l.azimuth = std::fmod(std::atan2(t[1], -t[0]) / kDeg + 360.0, 360.0);
    l.subLat = std::atan2(e[2], std::sqrt(e[0] * e[0] + e[1] * e[1])) / kDeg;
    l.subLon = std::atan2(e[1], e[0]) / kDeg;
    if (rangeAt(s, unixSeconds + 1.0, latDeg, lonDeg, altM, t2, e2))
        l.rangeRateKmS = std::sqrt(t2[0] * t2[0] + t2[1] * t2[1] + t2[2] * t2[2]) - l.rangeKm;
    l.valid = true;
    return l;
}

// Frequency shift heard on the ground for a transmit frequency, in Hz.
inline double dopplerHz(double rangeRateKmS, double freqMhz) { return -rangeRateKmS / 299792.458 * freqMhz * 1e6; }

struct Pass {
    bool found = false;
    double aos = 0, los = 0, tca = 0;   // unix seconds: rise, set, highest point
    double maxElevation = 0;
    double aosAzimuth = 0, losAzimuth = 0;
};

// The next pass above the horizon that ends after `from`. Searches `hours` ahead.
inline Pass nextPass(const Satellite& s, double from, double latDeg, double lonDeg, double altM = 0, double hours = 48) {
    Pass p;
    auto elevation = [&](double t) {
        double topo[3], ecef[3];
        if (!rangeAt(s, t, latDeg, lonDeg, altM, topo, ecef)) return -90.0;
        return std::asin(topo[2] / std::sqrt(topo[0] * topo[0] + topo[1] * topo[1] + topo[2] * topo[2])) / kDeg;
    };
    const double step = 60.0, end = from + hours * 3600.0;
    double t = from, prev = elevation(t);
    if (prev > 0) {                             // already up: walk back to the rise
        while (prev > 0 && t > from - 1800.0) { t -= step; prev = elevation(t); }
    }
    for (; t < end; t += step) {
        double cur = elevation(t + step);
        if (prev <= 0 && cur > 0) {
            double lo = t, hi = t + step;       // refine the rise to one second
            while (hi - lo > 1.0) { double mid = (lo + hi) / 2; (elevation(mid) > 0 ? hi : lo) = mid; }
            p.aos = hi;
            double u = hi, best = 0, bestT = hi;
            while (u < hi + 3600.0) {           // follow the pass
                double e = elevation(u);
                if (e > best) { best = e; bestT = u; }
                if (e <= 0 && u > hi + 5) break;
                u += 10.0;
            }
            lo = u - 10.0; hi = u;
            while (hi - lo > 1.0) { double mid = (lo + hi) / 2; (elevation(mid) > 0 ? lo : hi) = mid; }
            p.los = lo;
            p.tca = bestT;
            p.maxElevation = best;
            p.aosAzimuth = look(s, p.aos, latDeg, lonDeg, altM).azimuth;
            p.losAzimuth = look(s, p.los, latDeg, lonDeg, altM).azimuth;
            p.found = true;
            return p;
        }
        prev = cur;
    }
    return p;
}

}  // namespace sgp4
