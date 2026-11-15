#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "core/App.h"
#include "core/Lang.h"
#include "core/AprsIs.h"
#include "core/Mesh.h"
#include "core/Station.h"
#include "core/Storage.h"
#include "core/RangeTest.h"
#include "core/Ui.h"
#include "lib/Maidenhead.h"
#include "lib/MapData.h"

// Offline map from /HamOS/map on the SD card (coastlines, borders, cities)
// with APRS stations, Meshtastic nodes and your own position on top.
class MapApp : public App {
public:
    const char* name() const override { return T("Map"); }
    uint16_t color() const override { return 0x4C9F; }

    void onEnter() override {
        double lat, lon;
        if (!started_ && station::position(lat, lon)) {
            view_.lat = lat;
            view_.lon = lon;
            zoom_ = 7;                          // 150 km
        }
        started_ = true;
        view_.x0 = 0;
        view_.y0 = ui::BODY_Y;
        view_.w = ui::W;
        view_.h = ui::BODY_H;
        selected_ = -1;
    }

    void onTick(uint32_t now) override {
        if (now - lastMs_ > 10000) {            // stations move and appear in the background
            lastMs_ = now;
            dirty = true;
        }
    }

    void onKey(const KeyEvent& k) override {
        if (k.up()) view_.pan(0, 0.25);
        else if (k.down()) view_.pan(0, -0.25);
        else if (k.left()) view_.pan(-0.25, 0);
        else if (k.right()) view_.pan(0.25, 0);
        else if ((k.ch == '=' || k.ch == '+') && zoom_ < kZooms - 1) zoom_++;
        else if ((k.ch == '-' || k.ch == '_') && zoom_ > 0) zoom_--;
        else if (k.ch == 'h' || k.ch == 'H') {
            double lat, lon;
            if (station::position(lat, lon)) { view_.lat = lat; view_.lon = lon; }
        } else if (k.ch == 'c' || k.ch == 'C') cities_ = !cities_;
        else if (k.ch == 'g' || k.ch == 'G') grid_ = !grid_;
        else if (k.tab) {
            if (marks_.empty()) return;
            selected_ = (selected_ + 1) % (int)marks_.size();
        } else if (k.enter && selected_ >= 0 && selected_ < (int)marks_.size()) {
            view_.lat = marks_[(size_t)selected_].lat;
            view_.lon = marks_[(size_t)selected_].lon;
        } else {
            return;
        }
        dirty = true;
    }

    void draw() override {
        view_.kmWide = kZoomKm[zoom_];
        ui::clearBody();
        bool haveMap = drawLines();
        if (grid_) drawGrid();
        labels_.clear();
        collectMarks();
        drawMarks();
        if (cities_) drawCities();
        drawScale();
        if (!haveMap) {
            ui::textCentered(ui::BODY_Y + 30, T("No map data on the SD card"), ui::C_WARN);
            ui::textCentered(ui::BODY_Y + 44, T("Copy sdcard/HamOS/map from the repository"), ui::C_DIM);
        }
        std::string hint;
        if (selected_ >= 0 && selected_ < (int)marks_.size()) {
            const Mark& m = marks_[(size_t)selected_];
            double lat, lon;
            char buf[64];
            if (station::position(lat, lon))
                snprintf(buf, sizeof(buf), "%s %.1fkm %03.0f %s", m.label.c_str(), maidenhead::distanceKm(lat, lon, m.lat, m.lon),
                         maidenhead::bearingDeg(lat, lon, m.lat, m.lon), m.info.c_str());
            else
                snprintf(buf, sizeof(buf), "%s %s", m.label.c_str(), m.info.c_str());
            hint = ui::displayable(buf);
            if (ui::glyphCount(hint) > 39) hint = ui::wrapLines(hint, 39)[0];
        } else {
            hint = T("-/= zoom  arrows move  TAB stations");
        }
        ui::footer(hint.c_str());
    }

private:
    static constexpr int kZooms = 12;
    static constexpr double kZoomKm[kZooms] = {20000, 10000, 5000, 2500, 1200, 600, 300, 150, 75, 40, 20, 10};
    // RGB565
    static constexpr uint16_t kCoast = 0x4C9F, kBorder = 0x6B4D, kGrid = 0x2965, kGridText = 0x4A69, kCity = 0xC618;
    static constexpr uint16_t kInternet = 0xFFE0, kRadio = 0x07E0, kMesh = 0xF81F, kMe = 0xF800;

    struct Mark {
        double lat, lon;
        std::string label, info;
        uint16_t color;
        bool mesh;
    };
    struct Box {
        int x0, y0, x1, y1;
    };

    int level() const { return view_.kmWide > 2500 ? 0 : view_.kmWide > 300 ? 1 : 2; }

    static size_t readSd(void* path, uint32_t offset, void* buf, size_t len) {
        return storage::readAt((const char*)path, offset, buf, len);
    }

    bool openFile(const char* kind, mapdata::File& file, std::string& path) {
        path = std::string("/HamOS/map/") + kind + std::to_string(level()) + ".bin";
        return file.open(readSd, (void*)path.c_str());
    }

    bool drawLines() {
        mapdata::File file;
        std::string path;
        if (!openFile("lines", file, path)) return false;
        std::vector<std::pair<int, int>> tiles;
        mapdata::tilesFor(view_, file.tileDeg, tiles);
        std::vector<uint8_t> data;
        int top = view_.y0, bottom = view_.y0 + view_.h - 1;
        for (const auto& t : tiles) {
            if (!file.tile(t.first, t.second, data)) continue;
            mapdata::forEachLine(data, file.unit, [&](int kind, const double* la, const double* lo, int n) {
                uint16_t color = kind == 0 ? kCoast : kBorder;
                double px, py;
                view_.toScreen(la[0], lo[0], px, py);
                for (int i = 1; i < n; i++) {
                    double qx, qy;
                    view_.toScreen(la[i], lo[i], qx, qy);
                    // skip segments entirely off one side, and wrap-around jumps
                    bool off = (px < 0 && qx < 0) || (px >= ui::W && qx >= ui::W) || (py < top && qy < top) || (py > bottom && qy > bottom);
                    if (!off && std::fabs(qx - px) < ui::W * 2) {
                        int ax = (int)px, ay = (int)py, bx = (int)qx, by = (int)qy;
                        clipLine(ax, ay, bx, by, color);
                    }
                    px = qx;
                    py = qy;
                }
            });
        }
        return true;
    }

    // Draws only inside the map area (the status bar and footer stay clean).
    void clipLine(int x0, int y0, int x1, int y1, uint16_t color) {
        const int xmin = 0, xmax = ui::W - 1, ymin = view_.y0, ymax = view_.y0 + view_.h - 1;
        auto code = [&](int x, int y) { return (x < xmin) | (x > xmax) << 1 | (y < ymin) << 2 | (y > ymax) << 3; };
        int c0 = code(x0, y0), c1 = code(x1, y1);
        for (int guard = 0; guard < 8; guard++) {
            if (!(c0 | c1)) { ui::line(x0, y0, x1, y1, color); return; }
            if (c0 & c1) return;
            int c = c0 ? c0 : c1, x = 0, y = 0;
            double dx = x1 - x0, dy = y1 - y0;
            if (c & 8) { y = ymax; x = x0 + (int)std::lround(dx * (ymax - y0) / dy); }
            else if (c & 4) { y = ymin; x = x0 + (int)std::lround(dx * (ymin - y0) / dy); }
            else if (c & 2) { x = xmax; y = y0 + (int)std::lround(dy * (xmax - x0) / dx); }
            else { x = xmin; y = y0 + (int)std::lround(dy * (xmin - x0) / dx); }
            if (c == c0) { x0 = x; y0 = y; c0 = code(x0, y0); }
            else { x1 = x; y1 = y; c1 = code(x1, y1); }
        }
    }

    // Maidenhead lines: fields (20 x 10 degrees) when far out, squares (2 x 1) closer in.
    void drawGrid() {
        bool squares = view_.kmWide <= 1200;
        double stepLon = squares ? 2 : 20, stepLat = squares ? 1 : 10;
        double lo0 = std::floor((view_.lon - view_.halfLon()) / stepLon) * stepLon;
        double la0 = std::floor((view_.lat - view_.halfLat()) / stepLat) * stepLat;
        double x, y;
        for (double lo = lo0; lo <= view_.lon + view_.halfLon(); lo += stepLon) {
            view_.toScreen(view_.lat, lo, x, y);
            if (x >= 0 && x < ui::W) clipLine((int)x, view_.y0, (int)x, view_.y0 + view_.h - 1, kGrid);
        }
        for (double la = la0; la <= view_.lat + view_.halfLat(); la += stepLat) {
            view_.toScreen(la, view_.lon, x, y);
            if (y >= view_.y0 && y < view_.y0 + view_.h) clipLine(0, (int)y, ui::W - 1, (int)y, kGrid);
        }
        // Name of the square / field in the middle of each cell that is fully visible enough.
        if (view_.kmWide < 150) return;
        for (double lo = lo0; lo <= view_.lon + view_.halfLon(); lo += stepLon)
            for (double la = la0; la <= view_.lat + view_.halfLat(); la += stepLat) {
                view_.toScreen(la + stepLat / 2, lo + stepLon / 2, x, y);
                if (x < 14 || x > ui::W - 14 || y < view_.y0 + 6 || y > view_.y0 + view_.h - 10) continue;
                std::string g = maidenhead::fromLatLon(la + stepLat / 2, lo + stepLon / 2).substr(0, squares ? 4 : 2);
                ui::textOver((int)x - (int)g.size() * 3, (int)y - 4, g, kGridText);
            }
    }

    bool placeLabel(int x, int y, const std::string& text, uint16_t color) {
        int w = (int)ui::glyphCount(text) * 6;
        Box b = {x, y, x + w, y + 8};
        if (b.x1 > ui::W) { b.x0 -= w + 8; b.x1 -= w + 8; }
        if (b.x0 < 0 || b.y0 < view_.y0 || b.y1 > view_.y0 + view_.h) return false;
        for (const Box& o : labels_)
            if (b.x0 < o.x1 && o.x0 < b.x1 && b.y0 < o.y1 && o.y0 < b.y1) return false;
        labels_.push_back(b);
        ui::textOn(b.x0, b.y0, text, color, ui::C_BG);
        return true;
    }

    void collectMarks() {
        marks_.clear();
        for (const aprsis::Station& s : aprsis::stations()) {
            Mark m{s.lat, s.lon, s.call, s.comment, s.viaRadio() ? kRadio : kInternet, false};
            marks_.push_back(m);
        }
        for (const mesh::Node& n : mesh::nodes()) {
            if (!n.hasPosition) continue;
            Mark m{n.lat, n.lon, mesh::nodeName(n.id, true), mesh::nodeName(n.id), kMesh, true};
            marks_.push_back(m);
        }
        // nearest to the centre first, so TAB starts with what is in view
        std::sort(marks_.begin(), marks_.end(), [&](const Mark& a, const Mark& b) {
            return maidenhead::distanceKm(view_.lat, view_.lon, a.lat, a.lon) < maidenhead::distanceKm(view_.lat, view_.lon, b.lat, b.lon);
        });
        if (selected_ >= (int)marks_.size()) selected_ = -1;
    }

    void drawMarks() {
        double x, y;
        // range test receptions: where a packet arrived, coloured by signal
        for (const rangetest::Hit& h : rangetest::hits()) {
            if (!h.havePos) continue;
            view_.toScreen(h.lat, h.lon, x, y);
            if (x < 1 || x >= ui::W - 1 || y < view_.y0 + 1 || y >= view_.y0 + view_.h - 1) continue;
            uint16_t c = h.snr > 0 ? ui::C_OK : h.snr > -10 ? ui::C_NOTICE : h.snr > -15 ? ui::C_WARN : ui::C_ERR;
            ui::fill((int)x - 1, (int)y - 1, 3, 3, c);
        }
        // own position
        double lat, lon;
        if (station::position(lat, lon)) {
            view_.toScreen(lat, lon, x, y);
            if (x >= 4 && x < ui::W - 4 && y >= view_.y0 + 4 && y < view_.y0 + view_.h - 4) {
                ui::line((int)x - 5, (int)y, (int)x + 5, (int)y, kMe);
                ui::line((int)x, (int)y - 5, (int)x, (int)y + 5, kMe);
                ui::circle((int)x, (int)y, 3, kMe);
                labels_.push_back({(int)x - 5, (int)y - 5, (int)x + 5, (int)y + 5});
            }
        }
        for (size_t i = 0; i < marks_.size(); i++) {
            const Mark& m = marks_[i];
            view_.toScreen(m.lat, m.lon, x, y);
            if (x < 2 || x >= ui::W - 2 || y < view_.y0 + 2 || y >= view_.y0 + view_.h - 2) continue;
            int ix = (int)x, iy = (int)y;
            if (m.mesh) ui::triangle(ix, iy - 3, ix - 3, iy + 2, ix + 3, iy + 2, m.color);
            else ui::circle(ix, iy, 2, m.color, true);
            if ((int)i == selected_) ui::circle(ix, iy, 6, ui::C_TEXT);
            placeLabel(ix + 5, iy - 4, m.label, m.color);
        }
    }

    void drawCities() {
        mapdata::File file;
        std::string path;
        if (!openFile("cities", file, path)) return;
        std::vector<std::pair<int, int>> tiles;
        mapdata::tilesFor(view_, file.tileDeg, tiles);
        // Candidates from every visible tile, then the most important first.
        std::vector<mapdata::City> found;
        std::vector<uint8_t> data;
        for (const auto& t : tiles) {
            if (!file.tile(t.first, t.second, data, 2048)) continue;
            mapdata::forEachCity(data, [&](const mapdata::City& c) {
                double x, y;
                view_.toScreen(c.lat, c.lon, x, y);
                if (x >= 0 && x < ui::W && y >= view_.y0 && y < view_.y0 + view_.h) found.push_back(c);
                return found.size() < 400;
            });
        }
        std::sort(found.begin(), found.end(), [](const mapdata::City& a, const mapdata::City& b) {
            if ((a.rank <= 1) != (b.rank <= 1)) return a.rank <= 1;
            return a.population > b.population;
        });
        int shown = 0;
        for (const mapdata::City& c : found) {
            if (shown >= 14) break;
            double x, y;
            view_.toScreen(c.lat, c.lon, x, y);
            if (placeLabel((int)x + 3, (int)y - 4, c.name, kCity)) {
                ui::fill((int)x - 1, (int)y - 1, 3, 3, kCity);
                shown++;
            }
        }
    }

    void drawScale() {
        // A round distance about a quarter of the screen wide.
        double target = view_.kmWide / 4, nice = 1;
        while (nice * 10 <= target) nice *= 10;
        if (nice * 5 <= target) nice *= 5;
        else if (nice * 2 <= target) nice *= 2;
        int px = (int)std::lround(nice / view_.kmWide * ui::W);
        int y = view_.y0 + view_.h - 4;
        ui::fill(4, y, px, 2, ui::C_TEXT);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f km", nice);
        ui::textOn(4, y - 10, buf, ui::C_TEXT, ui::C_BG);
    }

    mapdata::View view_;
    int zoom_ = 4;
    bool started_ = false, cities_ = true, grid_ = false;
    int selected_ = -1;
    uint32_t lastMs_ = 0;
    std::vector<Mark> marks_;
    std::vector<Box> labels_;
};
