// Reader for the offline map files made by tools/map/make_map.py, and the
// map projection. Pure C++, host-testable: the file access is a callback.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mapdata {

// Reads `len` bytes at `offset` of one file; returns how many were read.
typedef size_t (*ReadFn)(void* user, uint32_t offset, void* buffer, size_t len);

struct File {
    ReadFn read = nullptr;
    void* user = nullptr;
    bool ok = false;
    int level = 0, tileDeg = 0, unit = 0, cols = 0, rows = 0;

    bool open(ReadFn fn, void* u) {
        read = fn;
        user = u;
        uint8_t h[12];
        ok = read && read(user, 0, h, 12) == 12 && memcmp(h, "HMAP", 4) == 0 && h[4] == 1 && h[6] > 0;
        if (!ok) return false;
        level = h[5];
        tileDeg = h[6];
        unit = h[7];
        cols = h[8] | (h[9] << 8);
        rows = h[10] | (h[11] << 8);
        ok = cols * tileDeg == 360 && rows * tileDeg == 180;
        return ok;
    }

    // Raw bytes of one tile, at most `maxBytes` (0 = all). Column wraps around the globe.
    bool tile(int col, int row, std::vector<uint8_t>& out, size_t maxBytes = 0) const {
        out.clear();
        if (!ok || row < 0 || row >= rows) return false;
        col = ((col % cols) + cols) % cols;
        uint8_t entry[8];
        if (read(user, 12 + 8u * (uint32_t)(row * cols + col), entry, 8) != 8) return false;
        uint32_t offset = (uint32_t)entry[0] | (uint32_t)entry[1] << 8 | (uint32_t)entry[2] << 16 | (uint32_t)entry[3] << 24;
        uint32_t size = (uint32_t)entry[4] | (uint32_t)entry[5] << 8 | (uint32_t)entry[6] << 16 | (uint32_t)entry[7] << 24;
        if (size == 0) return true;
        if (maxBytes && size > maxBytes) size = (uint32_t)maxBytes;
        out.resize(size);
        return read(user, offset, out.data(), size) == size;
    }
};

inline int32_t i32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
inline int16_t i16(const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); }

// Calls fn(kind, lat[], lon[], n) for every polyline in a lines tile.
// kind 0 = coastline, 1 = border. Stops at a damaged record.
template <class Fn>
void forEachLine(const std::vector<uint8_t>& tile, int unit, Fn fn) {
    double scale = std::pow(10.0, -unit);
    std::vector<double> lat, lon;
    size_t p = 0;
    while (p + 12 <= tile.size()) {
        int n = tile[p] | tile[p + 1] << 8, kind = tile[p + 2];
        int32_t lat0 = i32(&tile[p + 4]), lon0 = i32(&tile[p + 8]);
        p += 12;
        if (n < 2 || p + 4u * (size_t)n > tile.size()) return;
        lat.resize((size_t)n);
        lon.resize((size_t)n);
        for (int i = 0; i < n; i++, p += 4) {
            lat[(size_t)i] = (lat0 + i16(&tile[p])) * scale;
            lon[(size_t)i] = (lon0 + i16(&tile[p + 2])) * scale;
        }
        fn(kind, lat.data(), lon.data(), n);
    }
}

struct City {
    double lat, lon;
    int rank;               // 0 capital, 1 province capital, 2 district, 3 town, 4 small
    uint32_t population;
    std::string name;
};

// Cities of a tile, largest first. A tile read only partly ends cleanly at the last whole record.
template <class Fn>
void forEachCity(const std::vector<uint8_t>& tile, Fn fn) {
    size_t p = 0;
    while (p + 14 <= tile.size()) {
        size_t len = tile[p + 13];
        if (p + 14 + len > tile.size()) return;
        City c;
        c.lat = i32(&tile[p]) * 1e-5;
        c.lon = i32(&tile[p + 4]) * 1e-5;
        c.rank = tile[p + 8];
        c.population = (uint32_t)tile[p + 9] | (uint32_t)tile[p + 10] << 8 | (uint32_t)tile[p + 11] << 16 | (uint32_t)tile[p + 12] << 24;
        c.name.assign((const char*)&tile[p + 14], len);
        p += 14 + len;
        if (!fn(c)) return;
    }
}

// Equirectangular projection around the view centre (fine for regional maps).
struct View {
    double lat = 39, lon = 35;          // centre
    double kmWide = 1200;               // distance across the screen
    int x0 = 0, y0 = 0, w = 240, h = 108;

    double pxPerDegLat() const { return w / (kmWide / 111.32); }
    double pxPerDegLon() const { return pxPerDegLat() * std::cos(lat * 3.14159265358979 / 180.0); }

    static double wrap(double d) {      // -180..180
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return d;
    }

    void toScreen(double la, double lo, double& x, double& y) const {
        x = x0 + w / 2.0 + wrap(lo - lon) * pxPerDegLon();
        y = y0 + h / 2.0 - (la - lat) * pxPerDegLat();
    }

    void toLatLon(double x, double y, double& la, double& lo) const {
        la = lat - (y - y0 - h / 2.0) / pxPerDegLat();
        lo = wrap(lon + (x - x0 - w / 2.0) / pxPerDegLon());
    }

    // Degrees covered by the view (half-widths).
    double halfLat() const { return h / 2.0 / pxPerDegLat(); }
    double halfLon() const { return std::fmin(180.0, w / 2.0 / pxPerDegLon()); }

    void pan(double fx, double fy) {    // fractions of the view
        lon = wrap(lon + fx * 2 * halfLon());
        lat = std::fmax(-85.0, std::fmin(85.0, lat + fy * 2 * halfLat()));
    }
};

// Tiles (column, row) a view needs from a file with `tileDeg` sized tiles.
inline void tilesFor(const View& v, int tileDeg, std::vector<std::pair<int, int>>& out) {
    out.clear();
    int cols = 360 / tileDeg, rows = 180 / tileDeg;
    double la0 = v.lat - v.halfLat(), la1 = v.lat + v.halfLat();
    double lo0 = v.lon - v.halfLon(), lo1 = v.lon + v.halfLon();
    int r0 = (int)std::floor((std::fmax(la0, -90.0) + 90) / tileDeg), r1 = (int)std::floor((std::fmin(la1, 89.999) + 90) / tileDeg);
    int c0 = (int)std::floor((lo0 + 180) / tileDeg), c1 = (int)std::floor((lo1 + 180) / tileDeg);
    if (c1 - c0 + 1 > cols) { c0 = 0; c1 = cols - 1; }
    for (int r = std::max(r0, 0); r <= std::min(r1, rows - 1); r++)
        for (int c = c0; c <= c1; c++) out.push_back({((c % cols) + cols) % cols, r});
}

}  // namespace mapdata
