#include "Ui.h"
#include <M5Cardputer.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include "AprsIs.h"
#include "Gps.h"
#include "Mesh.h"
#include "Net.h"

namespace ui {

static auto& lcd() { return M5Cardputer.Display; }

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
    lcd().setTextSize(size);
    if (fillBg) lcd().setTextColor(fg, bg);
    else lcd().setTextColor(fg);                // transparent background
    lcd().setTextDatum(top_left);
    float cell = 6 * size;
    std::string run;
    size_t runStart = 0, index = 0;
    auto flush = [&]() {
        if (!run.empty()) lcd().drawString(run.c_str(), x + (int)(runStart * cell), y);
        run.clear();
    };
    for (size_t i = 0; i < s.size();) {
        uint32_t code = nextCodePoint(s, i);
        const Glyph* g = code >= 128 ? glyphFor(code) : nullptr;
        if (g) {
            flush();
            int gx = x + (int)(index * cell);
            if (fillBg) lcd().fillRect(gx, y, (int)(cell + 0.5f), (int)(8 * size + 0.5f), bg);
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 8; row++)
                    if (g->cols[col] >> row & 1) {
                        int px = gx + (int)(col * size), py = y + (int)(row * size);
                        int pw = gx + (int)((col + 1) * size) - px, ph = y + (int)((row + 1) * size) - py;
                        lcd().fillRect(px, py, pw, ph, fg);
                    }
        } else {
            if (run.empty()) runStart = index;
            run += (code >= 32 && code < 127) ? (char)code : '?';
        }
        index++;
    }
    flush();
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
    lcd().setRotation(1);
    lcd().setTextFont(1);
    lcd().fillScreen(C_BG);
}

void statusBar(const char* title) {
    lcd().fillRect(0, 0, W, STATUS_H, C_BAR);
    drawText(notice.empty() ? std::string(title) : notice, 4, STATUS_H / 2, 1, LEFT, true, notice.empty() ? C_TEXT : C_NOTICE, C_BAR);

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
}

void footer(const char* hint) {
    lcd().fillRect(0, H - FOOTER_H, W, FOOTER_H, C_FOOT);
    drawText(hint, 4, H - FOOTER_H / 2 - 1, 1, LEFT, true, C_TEXT, C_FOOT);
}

void fill(int x, int y, int w, int h, uint16_t color) { lcd().fillRect(x, y, w, h, color); }
void line(int x0, int y0, int x1, int y1, uint16_t color) { lcd().drawLine(x0, y0, x1, y1, color); }
void pixel(int x, int y, uint16_t color) { lcd().drawPixel(x, y, color); }
void circle(int x, int y, int r, uint16_t color, bool filled) {
    if (filled) lcd().fillCircle(x, y, r, color);
    else lcd().drawCircle(x, y, r, color);
}
void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) { lcd().fillTriangle(x0, y0, x1, y1, x2, y2, color); }
void frame(int x, int y, int w, int h, uint16_t color) { lcd().drawRect(x, y, w, h, color); }
void image(int x, int y, int w, int h, const uint16_t* data) {
    // With byte swapping off, M5GFX reads uint16_t pixel data as high byte first.
    lcd().setSwapBytes(false);
    lcd().pushImage(x, y, w, h, data);
}

void tileLabel(int x, int yMiddle, const char* s, uint16_t bg) { drawText(s, x, yMiddle, 1.5, LEFT, true, C_TEXT, bg); }

void formRow(int row, const char* label, const std::string& value, bool active, uint16_t valueColor) {
    int y = BODY_Y + 2 + row * ROW_H;
    uint16_t bg = active ? C_FOOT : C_BG;
    lcd().fillRect(0, y, W, ROW_H, bg);
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

void clearBody() { lcd().fillRect(0, BODY_Y, W, BODY_H, C_BG); }

void text(int x, int y, const std::string& s, uint16_t color, float size) { drawText(s, x, y, size, LEFT, false, color, C_BG); }

void textOver(int x, int y, const std::string& s, uint16_t color) { drawText(s, x, y, 1, LEFT, false, color, C_BG, false); }

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
    lcd().fillRect(0, top, W, rows * ROW_H, C_BG);
    for (int row = 0; row < rows; row++) {
        int i = scroll + row;
        if (i >= (int)items.size()) break;
        int y = top + row * ROW_H;
        uint16_t bg = (i == selected) ? C_SEL : C_BG;
        if (i == selected) lcd().fillRect(0, y, W, ROW_H, bg);
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
    lcd().fillRect(6, y + 12, W - 12, 20, C_FOOT);
    // Show the tail if the value is wider than the box.
    const size_t fit = 24;
    std::string shown = value.size() > fit ? value.substr(value.size() - fit) : value;
    drawText(shown + "_", 10, y + 22, 1.5, LEFT, true, C_TEXT, C_FOOT);
}

}  // namespace ui
