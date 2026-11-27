#include "Ui.h"
#include <M5Cardputer.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "AprsIs.h"
#include "Keyer.h"
#include "Gps.h"
#include "Mesh.h"
#include "Net.h"
#include "Usb.h"

namespace ui {

// ---- flicker-free drawing with little memory ----
// Screens clear an area (the body, a list row, the status bar) and draw it
// again. Done straight on the display, that shows as a black flash. Instead, a
// large fill does not go out at once: it starts a "region", and what is drawn
// into it afterwards is recorded. present() (once per main loop) renders each
// region in strips of 16 lines into a small buffer (240 x 16 pixels, 7.5 KB)
// and sends them: the display only ever sees finished pixels. Drawing that is
// not inside a region goes to the display directly, as before.

enum OpKind : uint8_t { OP_FILL, OP_STR, OP_GLYPH, OP_LINE, OP_CIRCLE, OP_DISC, OP_TRI, OP_FRAME, OP_IMG };
struct Op {
    OpKind kind;
    bool fillBg = false;
    int16_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;     // coordinates, meaning depends on the kind
    int16_t bx = 0, by = 0, bw = 0, bh = 0;               // bounding box
    uint16_t color = 0, bg = 0;
    float size = 1;
    std::string str;                                      // OP_STR text, OP_GLYPH: index of the glyph
    std::vector<uint16_t> img;                            // OP_IMG pixels
};
struct Region {
    int16_t x, y, w, h;
    uint16_t color;
    size_t firstOp;         // ops recorded from here on can fall into this region
};

template <class G>
static void rasterize(G& g, const Op& o, int dy);

#ifdef HAMOS_SIM
static auto& lcd() { return M5Cardputer.Display; }
static void emit(Op&& o) { rasterize(lcd(), o, 0); }
void setBuffered(bool) {}
bool buffered() { return false; }
void present() {}
#else
static void present_();
static auto& lcd() { return M5Cardputer.Display; }
static M5Canvas* strip = nullptr;           // 240 x kStrip pixels
static constexpr int kStrip = 16;
static constexpr size_t kMaxOps = 700, kMaxBytes = 16 * 1024;
static bool enabled = true;
static std::vector<Region> regions;
static std::vector<Op> ops;
static size_t opBytes = 0;

static bool inside(const Region& r, int x, int y, int w, int h) {
    return x >= r.x && y >= r.y && x + w <= r.x + r.w && y + h <= r.y + r.h;
}
static bool overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void emit(Op&& o) {
    if (!strip || !enabled) {
        rasterize(lcd(), o, 0);
        return;
    }
    bool within = false, touching = false;
    for (const Region& r : regions) {
        if (inside(r, o.bx, o.by, o.bw, o.bh)) within = true;
        if (overlaps(r.x, r.y, r.w, r.h, o.bx, o.by, o.bw, o.bh)) touching = true;
    }
    // A large fill outside the regions starts a new one (nothing goes out yet).
    if (o.kind == OP_FILL && !within && o.bw * o.bh >= W * 8 && regions.size() < 24) {
        Region r{o.bx, o.by, o.bw, o.bh, o.color, ops.size()};
        if (touching) ops.push_back(o);     // so the older regions under it show it too
        regions.push_back(r);
        return;
    }
    if (!within) rasterize(lcd(), o, 0);    // outside every region: straight to the display
    if (!touching) return;
    size_t bytes = sizeof(Op) + o.str.size() + o.img.size() * 2;
    if (ops.size() >= kMaxOps || opBytes + bytes > kMaxBytes) {
        // Too much for one frame (a map with thousands of lines): show what is
        // there now and draw the rest straight on the display.
        present_();
        if (within) rasterize(lcd(), o, 0);
        enabled = false;                    // until the end of this main loop
        return;
    }
    opBytes += bytes;
    ops.push_back(std::move(o));
}

static void present_() {
    auto& d = M5Cardputer.Display;
    for (const Region& r : regions) {
        for (int y0 = r.y; y0 < r.y + r.h; y0 += kStrip) {
            int h = std::min(kStrip, r.y + r.h - y0);
            strip->clearClipRect();
            strip->setClipRect(r.x, 0, r.w, h);
            strip->fillRect(r.x, 0, r.w, h, r.color);
            for (size_t i = r.firstOp; i < ops.size(); i++) {
                const Op& o = ops[i];
                if (overlaps(o.bx, o.by, o.bw, o.bh, r.x, y0, r.w, h)) rasterize(*strip, o, y0);
            }
            d.setClipRect(r.x, y0, r.w, h);
            strip->pushSprite(&d, 0, y0);
            d.clearClipRect();
        }
    }
    regions.clear();
    ops.clear();
    opBytes = 0;
}

void present() {
    if (strip && !regions.empty()) present_();
    regions.clear();
    ops.clear();
    opBytes = 0;
    enabled = true;
}
void setBuffered(bool on) {
    if (!on && strip) present_();
    if (on && !strip) {
        M5Canvas* c = new M5Canvas(&M5Cardputer.Display);
        c->setColorDepth(16);
        c->setTextFont(1);
        if (c->createSprite(W, kStrip)) strip = c;
        else delete c;
    } else if (!on && strip) {
        strip->deleteSprite();
        delete strip;
        strip = nullptr;
    }
}
bool buffered() { return strip != nullptr; }
#endif

static void bbox(Op& o, int x, int y, int w, int h) {
    o.bx = (int16_t)x;
    o.by = (int16_t)y;
    o.bw = (int16_t)w;
    o.bh = (int16_t)h;
}

// Every drawing step also goes to the computer while it mirrors the screen.
static void rect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    Op o;
    o.kind = OP_FILL;
    o.a = (int16_t)x, o.b = (int16_t)y, o.c = (int16_t)w, o.d = (int16_t)h;
    o.color = c;
    bbox(o, x, y, w, h);
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorFill(x, y, w, h, c);
}

// ---- text with Turkish letters ----
// The built-in 6x8 font has no Turkish letters, so these twelve are drawn from
// their own bitmaps: five columns each, bit 0 = top row, bit 7 = bottom row.
struct Glyph {
    uint16_t code;
    uint8_t cols[5];
};
static const Glyph kGlyphs[] = {
    {0x00E7, {0x38, 0x44, 0xC4, 0x44, 0x20}},   // ç
    {0x00C7, {0x3E, 0x41, 0xC1, 0x41, 0x22}},   // Ç
    {0x011F, {0x25, 0xAA, 0xA9, 0xA9, 0x7C}},   // ğ
    {0x011E, {0x3C, 0x81, 0x92, 0x91, 0x74}},   // Ğ
    {0x0131, {0x00, 0x44, 0x7C, 0x40, 0x00}},   // ı
    {0x0130, {0x00, 0x84, 0xFD, 0x84, 0x00}},   // İ
    {0x00F6, {0x38, 0x45, 0x44, 0x45, 0x38}},   // ö
    {0x00D6, {0x7C, 0x83, 0x82, 0x83, 0x7C}},   // Ö
    {0x015F, {0x48, 0x54, 0xD4, 0x54, 0x20}},   // ş
    {0x015E, {0x46, 0x49, 0xC9, 0x49, 0x31}},   // Ş
    {0x00FC, {0x3C, 0x41, 0x40, 0x21, 0x7C}},   // ü
    {0x00DC, {0x7C, 0x81, 0x80, 0x81, 0x7C}},   // Ü
};

static const Glyph* glyphFor(uint32_t code) {
    for (const Glyph& g : kGlyphs)
        if (g.code == code) return &g;
    return nullptr;
}

// Next code point of a UTF-8 string; advances `i`. Malformed bytes count as one '?'.
static uint32_t nextCodePoint(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (len == 0 || i + len > s.size()) { i++; return '?'; }
    uint32_t code = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t k = 1; k < len; k++) {
        unsigned char cc = (unsigned char)s[i + k];
        if ((cc & 0xC0) != 0x80) { i++; return '?'; }
        code = (code << 6) | (cc & 0x3Fu);
    }
    i += len;
    return code;
}

size_t glyphCount(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        nextCodePoint(s, i);
        n++;
    }
    return n;
}

std::string displayable(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        size_t start = i;
        uint32_t code = nextCodePoint(in, i);
        if ((code >= 32 && code < 127) || code == '\n') out += (char)code;
        else if (glyphFor(code)) out.append(in, start, i - start);
        else if (code == '\t') out += ' ';
        else if (code != '\r') out += '?';
    }
    return out;
}

// Draws text with its top left corner at (x, y): runs of plain characters with
// the built-in font, Turkish letters from the bitmaps above.
static void drawRun(const std::string& s, int x, int y, float size, uint16_t fg, uint16_t bg, bool fillBg) {
    float cell = 6 * size;
    std::string run;
    size_t runStart = 0, index = 0;
    auto flush = [&]() {
        if (!run.empty()) {
            int rx = x + (int)(runStart * cell);
            Op o;
            o.kind = OP_STR;
            o.a = (int16_t)rx, o.b = (int16_t)y;
            o.size = size, o.color = fg, o.bg = bg, o.fillBg = fillBg;
            bbox(o, rx, y, (int)(run.size() * cell + 1), (int)(8 * size + 1));
            if (usb::mirroring()) usb::mirrorText(rx, y, size, fg, fillBg ? (int)bg : -1, run);
            o.str = run;
            emit(std::move(o));
        }
        run.clear();
    };
    for (size_t i = 0; i < s.size();) {
        uint32_t code = nextCodePoint(s, i);
        const Glyph* g = code >= 128 ? glyphFor(code) : nullptr;
        if (g) {
            flush();
            int gx = x + (int)(index * cell);
            Op o;
            o.kind = OP_GLYPH;
            o.a = (int16_t)gx, o.b = (int16_t)y, o.c = (int16_t)(g - kGlyphs);
            o.size = size, o.color = fg, o.bg = bg, o.fillBg = fillBg;
            bbox(o, gx, y, (int)(cell + 1), (int)(8 * size + 1));
            emit(std::move(o));
            if (usb::mirroring()) {             // the computer gets the letter as small squares
                if (fillBg) usb::mirrorFill(gx, y, (int)(cell + 0.5f), (int)(8 * size + 0.5f), bg);
                for (int col = 0; col < 5; col++)
                    for (int row = 0; row < 8; row++)
                        if (g->cols[col] >> row & 1) {
                            int px = gx + (int)(col * size), py = y + (int)(row * size);
                            usb::mirrorFill(px, py, gx + (int)((col + 1) * size) - px, y + (int)((row + 1) * size) - py, fg);
                        }
            }
        } else {
            if (run.empty()) runStart = index;
            run += (code >= 32 && code < 127) ? (char)code : '?';
        }
        index++;
    }
    flush();
}

// One recorded drawing step onto `g` (the display, or the strip buffer whose
// top line is screen line `dy`).
template <class G>
static void rasterize(G& g, const Op& o, int dy) {
    switch (o.kind) {
        case OP_FILL: g.fillRect(o.a, o.b - dy, o.c, o.d, o.color); break;
        case OP_STR:
            g.setTextSize(o.size);
            if (o.fillBg) g.setTextColor(o.color, o.bg);
            else g.setTextColor(o.color);           // transparent background
            g.setTextDatum(top_left);
            g.drawString(o.str.c_str(), o.a, o.b - dy);
            break;
        case OP_GLYPH: {
            const Glyph& gl = kGlyphs[o.c];
            float size = o.size, cell = 6 * size;
            int gx = o.a, y = o.b - dy;
            if (o.fillBg) g.fillRect(gx, y, (int)(cell + 0.5f), (int)(8 * size + 0.5f), o.bg);
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 8; row++)
                    if (gl.cols[col] >> row & 1) {
                        int px = gx + (int)(col * size), py = y + (int)(row * size);
                        g.fillRect(px, py, gx + (int)((col + 1) * size) - px, y + (int)((row + 1) * size) - py, o.color);
                    }
            break;
        }
        case OP_LINE: g.drawLine(o.a, o.b - dy, o.c, o.d - dy, o.color); break;
        case OP_CIRCLE: g.drawCircle(o.a, o.b - dy, o.c, o.color); break;
        case OP_DISC: g.fillCircle(o.a, o.b - dy, o.c, o.color); break;
        case OP_TRI: g.fillTriangle(o.a, o.b - dy, o.c, o.d - dy, o.e, o.f - dy, o.color); break;
        case OP_FRAME: g.drawRect(o.a, o.b - dy, o.c, o.d, o.color); break;
        case OP_IMG:
            // With byte swapping off, M5GFX reads uint16_t pixel data as high byte first.
            g.setSwapBytes(false);
            g.pushImage(o.a, o.b - dy, o.c, o.d, o.img.data());
            break;
    }
}

enum Align { LEFT, CENTER, RIGHT };

static void drawText(const std::string& s, int x, int y, float size, Align align, bool middle, uint16_t fg, uint16_t bg,
                     bool fillBg = true) {
    int width = (int)(glyphCount(s) * 6 * size);
    if (align == CENTER) x -= width / 2;
    else if (align == RIGHT) x -= width;
    if (middle) y -= (int)(8 * size) / 2;
    drawRun(s, x, y, size, fg, bg, fillBg);
}

static std::string notice;
void setNotice(const std::string& text) { notice = text; }

void begin() {
    M5Cardputer.Display.setRotation(0);   // M5GFX 0.3+ sets the Cardputer panel up as 240x135 at rotation 0
    M5Cardputer.Display.setTextFont(1);
    M5Cardputer.Display.fillScreen(C_BG);
}

void statusBar(const char* title) {
    rect(0, 0, W, STATUS_H, C_BAR);

    char right[48];
    char clock[8] = "--:--";
    if (net::timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        gmtime_r(&now, &t);
        snprintf(clock, sizeof(clock), "%02d:%02d", t.tm_hour, t.tm_min);
    }
    char unread[12] = "";
    if (mesh::unread() > 0) snprintf(unread, sizeof(unread), " M%d", mesh::unread() > 99 ? 99 : mesh::unread());
    if (aprsis::unread() > 0) {
        size_t used = strlen(unread);
        snprintf(unread + used, sizeof(unread) - used, " A%d", aprsis::unread() > 9 ? 9 : aprsis::unread());
    }
    snprintf(right, sizeof(right), "%sz%s%s%s %d%%", clock, unread, gps::hasFix() ? " GPS" : "",
             net::connected() ? " WiFi" : "", (int)M5Cardputer.Power.getBatteryLevel());
    drawText(right, W - 4, STATUS_H / 2, 1, RIGHT, true, C_TEXT, C_BAR);
    // The CW keyer keys the radio: a red TX left of the clock, on every page.
    int txRoom = 0;
    if (keyer::onAir()) {
        int x = W - 4 - (int)strlen(right) * 6 - 18;
        rect(x - 2, 1, 16, STATUS_H - 2, C_ERR);
        drawText("TX", x, STATUS_H / 2, 1, LEFT, true, C_TEXT, C_ERR);
        txRoom = 3;
    }
    // The title or notice on the left, cut to the room left of the clock.
    std::string left = notice.empty() ? std::string(title) : notice;
    size_t room = (size_t)((W - 8 - (int)strlen(right) * 6 - 6) / 6 - txRoom), n = 0, cut = 0;
    while (cut < left.size() && n < room) {
        unsigned char c = (unsigned char)left[cut];
        cut += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        n++;
    }
    drawText(left.substr(0, cut), 4, STATUS_H / 2, 1, LEFT, true, notice.empty() ? C_TEXT : C_NOTICE, C_BAR);
}

void footer(const char* hint) {
    rect(0, H - FOOTER_H, W, FOOTER_H, C_FOOT);
    drawText(hint, 4, H - FOOTER_H / 2 - 1, 1, LEFT, true, C_TEXT, C_FOOT);
}

void fill(int x, int y, int w, int h, uint16_t color) { rect(x, y, w, h, color); }
void line(int x0, int y0, int x1, int y1, uint16_t color) {
    Op o;
    o.kind = OP_LINE;
    o.a = (int16_t)x0, o.b = (int16_t)y0, o.c = (int16_t)x1, o.d = (int16_t)y1;
    o.color = color;
    bbox(o, std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1);
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorLine(x0, y0, x1, y1, color);
}
void pixel(int x, int y, uint16_t color) { rect(x, y, 1, 1, color); }
void circle(int x, int y, int r, uint16_t color, bool filled) {
    Op o;
    o.kind = filled ? OP_DISC : OP_CIRCLE;
    o.a = (int16_t)x, o.b = (int16_t)y, o.c = (int16_t)r;
    o.color = color;
    bbox(o, x - r, y - r, 2 * r + 1, 2 * r + 1);
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorCircle(x, y, r, color, filled);
}
void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    Op o;
    o.kind = OP_TRI;
    o.a = (int16_t)x0, o.b = (int16_t)y0, o.c = (int16_t)x1, o.d = (int16_t)y1, o.e = (int16_t)x2, o.f = (int16_t)y2;
    o.color = color;
    int lx = std::min(x0, std::min(x1, x2)), ly = std::min(y0, std::min(y1, y2));
    bbox(o, lx, ly, std::max(x0, std::max(x1, x2)) - lx + 1, std::max(y0, std::max(y1, y2)) - ly + 1);
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorTriangle(x0, y0, x1, y1, x2, y2, color);
}
void frame(int x, int y, int w, int h, uint16_t color) {
    Op o;
    o.kind = OP_FRAME;
    o.a = (int16_t)x, o.b = (int16_t)y, o.c = (int16_t)w, o.d = (int16_t)h;
    o.color = color;
    bbox(o, x, y, w, h);
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorFrame(x, y, w, h, color);
}
void image(int x, int y, int w, int h, const uint16_t* data) {
    if (w <= 0 || h <= 0) return;
    Op o;
    o.kind = OP_IMG;
    o.a = (int16_t)x, o.b = (int16_t)y, o.c = (int16_t)w, o.d = (int16_t)h;
    bbox(o, x, y, w, h);
#ifndef HAMOS_SIM
    // Recorded only when it lands in a region (a copy of the pixels); otherwise drawn at once.
    bool keep = false;
    if (strip && enabled)
        for (const Region& r : regions) keep = keep || overlaps(r.x, r.y, r.w, r.h, x, y, w, h);
    if (keep && (size_t)w * h <= 4096) o.img.assign(data, data + (size_t)w * h);
    else {
        if (keep) present_();
        lcd().setSwapBytes(false);
        lcd().pushImage(x, y, w, h, data);
        if (usb::mirroring()) usb::mirrorImage(x, y, w, h, data);
        return;
    }
#else
    o.img.assign(data, data + (size_t)w * h);
#endif
    emit(std::move(o));
    if (usb::mirroring()) usb::mirrorImage(x, y, w, h, data);
}

void tileLabel(int x, int yMiddle, const char* s, uint16_t bg) { drawText(s, x, yMiddle, 1.5, LEFT, true, C_TEXT, bg); }

void formRow(int row, const char* label, const std::string& value, bool active, uint16_t valueColor) {
    int y = BODY_Y + 2 + row * ROW_H;
    uint16_t bg = active ? C_FOOT : C_BG;
    rect(0, y, W, ROW_H, bg);
    drawText(label, 6, y + ROW_H / 2, 1, LEFT, true, active ? C_SEL : C_DIM, bg);
    drawText(value + (active ? "_" : ""), 84, y + ROW_H / 2, 1, LEFT, true, valueColor, bg);
}

std::string num(double v, int decimals) {
    if (std::isnan(v)) return "-";
    if (std::isinf(v)) return "inf";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

void clearBody() { rect(0, BODY_Y, W, BODY_H, C_BG); }

void text(int x, int y, const std::string& s, uint16_t color, float size) { drawText(s, x, y, size, LEFT, false, color, C_BG); }

void textOver(int x, int y, const std::string& s, uint16_t color, float size) { drawText(s, x, y, size, LEFT, false, color, C_BG, false); }

void textOn(int x, int y, const std::string& s, uint16_t color, uint16_t background) {
    drawText(s, x, y, 1, LEFT, false, color, background);
}

std::string ascii(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            out += (c >= 32 && c < 127) ? (char)c : ' ';
            i++;
            continue;
        }
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        unsigned code = len == 2 && i + 1 < in.size() ? ((c & 0x1Fu) << 6) | ((unsigned char)in[i + 1] & 0x3Fu) : 0;
        switch (code) {
            case 0xE7: out += 'c'; break;   case 0xC7: out += 'C'; break;
            case 0x11F: out += 'g'; break;  case 0x11E: out += 'G'; break;
            case 0x131: out += 'i'; break;  case 0x130: out += 'I'; break;
            case 0xF6: out += 'o'; break;   case 0xD6: out += 'O'; break;
            case 0x15F: out += 's'; break;  case 0x15E: out += 'S'; break;
            case 0xFC: out += 'u'; break;   case 0xDC: out += 'U'; break;
            default: out += '?';
        }
        i += len;
        while (i < in.size() && ((unsigned char)in[i] & 0xC0) == 0x80 && len == 1) i++;   // stray continuation bytes
    }
    return out;
}

std::vector<std::string> wrapLines(const std::string& text, size_t width) {
    std::vector<std::string> out;
    if (width == 0) return out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string para = text.substr(pos, nl - pos);
        std::vector<size_t> starts;                 // byte offset of every letter
        for (size_t i = 0; i < para.size();) {
            starts.push_back(i);
            nextCodePoint(para, i);
        }
        size_t n = starts.size(), a = 0;
        if (n == 0 && nl < text.size()) out.push_back("");
        while (a < n) {
            if (n - a <= width) {
                out.push_back(para.substr(starts[a]));
                break;
            }
            size_t k = a + width;                   // break after the last space that fits, else mid-word
            for (size_t j = a + width; j > a; j--)
                if (para[starts[j - 1]] == ' ') { k = j; break; }
            out.push_back(para.substr(starts[a], starts[k] - starts[a]));
            a = k;
        }
        if (nl >= text.size()) break;
        pos = nl + 1;
    }
    return out;
}

void textCentered(int y, const std::string& s, uint16_t color, float size) {
    drawText(s, W / 2, y, size, CENTER, false, color, C_BG);
}

void list(const std::vector<std::string>& items, int selected, int& scroll, const std::vector<uint16_t>* colors, int top,
          int rows) {
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + rows) scroll = selected - rows + 1;
    if (scroll < 0) scroll = 0;
    rect(0, top, W, rows * ROW_H, C_BG);
    for (int row = 0; row < rows; row++) {
        int i = scroll + row;
        if (i >= (int)items.size()) break;
        int y = top + row * ROW_H;
        uint16_t bg = (i == selected) ? C_SEL : C_BG;
        if (i == selected) rect(0, y, W, ROW_H, bg);
        drawText(items[(size_t)i], 6, y + ROW_H / 2, 1, LEFT, true, colors && i < (int)colors->size() ? (*colors)[(size_t)i] : C_TEXT, bg);
    }
}

bool listNav(const KeyEvent& k, int& selected, int count) {
    if (count <= 0) return false;
    int before = selected;
    if (k.up()) selected = (selected + count - 1) % count;
    if (k.down()) selected = (selected + 1) % count;
    return selected != before;
}

bool TextInput::handle(const KeyEvent& k) {
    if (k.del) {
        if (value.empty()) return false;
        value.pop_back();
        return true;
    }
    if (k.ch >= 32 && k.ch < 127 && value.size() < maxLen) {
        value += upper ? (char)toupper((unsigned char)k.ch) : k.ch;
        return true;
    }
    return false;
}

void TextInput::draw(int y, const char* label) const {
    text(6, y, label, C_DIM);
    rect(6, y + 12, W - 12, 20, C_FOOT);
    // Show the tail if the value is wider than the box.
    const size_t fit = 24;
    std::string shown = value.size() > fit ? value.substr(value.size() - fit) : value;
    drawText(shown + "_", 10, y + 22, 1.5, LEFT, true, C_TEXT, C_FOOT);
}

}  // namespace ui
