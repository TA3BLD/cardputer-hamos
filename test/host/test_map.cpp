// Host-side test of the map reader against the files in sdcard/HamOS/map:
//   g++ -std=c++17 -I src test/host/test_map.cpp -o test/host/t.out && test/host/t.out sdcard/HamOS/map
#include <cstdio>
#include <string>
#include "lib/MapData.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static size_t readFile(void* user, uint32_t offset, void* buf, size_t len) {
    FILE* f = (FILE*)user;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 0;
    return fread(buf, 1, len, f);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "sdcard/HamOS/map";
    // Projection
    mapdata::View v;
    v.lat = 38.42; v.lon = 27.14; v.kmWide = 100; v.w = 240; v.h = 108; v.x0 = 0; v.y0 = 14;
    double x, y, la, lo;
    v.toScreen(38.42, 27.14, x, y);
    CHECK(std::fabs(x - 120) < 1e-9 && std::fabs(y - 68) < 1e-9);
    v.toScreen(38.42, 27.14 + 50 / (111.32 * std::cos(38.42 * M_PI / 180)), x, y);
    CHECK(std::fabs(x - 240) < 0.01);                        // 50 km east = right edge of a 100 km view
    v.toLatLon(30, 90, la, lo);
    v.toScreen(la, lo, x, y);
    CHECK(std::fabs(x - 30) < 1e-6 && std::fabs(y - 90) < 1e-6);
    mapdata::View w = v;
    w.lon = 179.5; w.kmWide = 400;
    w.toScreen(0, -179.5, x, y);
    CHECK(x > 120 && x < 240);                               // across the date line
    std::vector<std::pair<int, int>> tiles;
    mapdata::tilesFor(w, 2, tiles);
    bool east = false, west = false;
    for (auto& t : tiles) { east |= t.first == 179; west |= t.first == 0; }
    CHECK(east && west);
    w.pan(0, 10);
    CHECK(w.lat <= 85);

    // Files
    for (int level = 0; level < 3; level++) {
        std::string path = dir + "/lines" + std::to_string(level) + ".bin";
        FILE* f = fopen(path.c_str(), "rb");
        CHECK(f != nullptr);
        if (!f) continue;
        mapdata::File file;
        CHECK(file.open(readFile, f) && file.level == level);
        // The tile around Izmir has coastline near the city.
        int col = (int)((27.14 + 180) / file.tileDeg), row = (int)((38.42 + 90) / file.tileDeg);
        std::vector<uint8_t> data;
        CHECK(file.tile(col, row, data) && !data.empty());
        double nearest = 1e9;
        int lines = 0;
        mapdata::forEachLine(data, file.unit, [&](int, const double* la, const double* lo, int n) {
            lines++;
            for (int i = 0; i < n; i++) {
                double d = std::hypot(la[i] - 38.42, (lo[i] - 27.14) * 0.78);
                if (d < nearest) nearest = d;
            }
        });
        printf("  level %d: %d polylines in the Izmir tile, coast %.2f deg from the city\n", level, lines, nearest);
        // Streaming point by point gives exactly the same lines and points.
        {
            std::vector<double> whole, streamed;
            int wholeLines = 0, streamedLines = 0;
            mapdata::forEachLine(data, file.unit, [&](int kind, const double* la, const double* lo, int n) {
                wholeLines++;
                for (int i = 0; i < n; i++) { whole.push_back(kind); whole.push_back(la[i]); whole.push_back(lo[i]); }
            });
            CHECK(file.streamLines(col, row, [&](int kind, int i, double la, double lo) {
                if (i == 0) streamedLines++;
                streamed.push_back(kind); streamed.push_back(la); streamed.push_back(lo);
            }));
            CHECK(wholeLines == streamedLines && whole == streamed);
        }
        CHECK(lines > 0 && nearest < (level == 0 ? 1.0 : 0.15));
        CHECK(file.tile(col + file.cols, row, data));       // wraps around
        CHECK(!file.tile(col, file.rows, data));
        fclose(f);
    }
    FILE* f = fopen((dir + "/cities2.bin").c_str(), "rb");
    mapdata::File cities;
    CHECK(f && cities.open(readFile, f));
    if (f) {
        std::vector<uint8_t> data;
        cities.tile((int)((27.14 + 180) / 2), (int)((38.42 + 90) / 2), data, 600);   // partial read
        std::vector<std::string> names;
        mapdata::forEachCity(data, [&](const mapdata::City& c) { names.push_back(c.name); return true; });
        CHECK(!names.empty() && names[0] == "\xc4\xb0zmir");        // largest first, Turkish letters kept
        printf("  cities near Izmir: %zu read from the first 600 bytes\n", names.size());
        fclose(f);
    }
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
