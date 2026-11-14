// A small JSON reader that keeps the whole document as a tree, for web APIs
// (GitHub releases, POTA, SOTA, PSKReporter). MiniJson.h is the faster flat
// reader for the USB link.
//
//   jdom::Value v;
//   if (jdom::parse(text, v)) std::string tag = v["tag_name"].str();
//
// Missing keys and out-of-range indexes give a shared null value, so lookups
// can be chained without checks: v["assets"][0]["size"].num().
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace jdom {

struct Value {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> items;                               // ARR
    std::vector<std::pair<std::string, Value>> members;     // OBJ, in document order

    static const Value& null() {
        static const Value v;
        return v;
    }
    bool isNull() const { return type == NUL; }
    bool isArray() const { return type == ARR; }
    bool isObject() const { return type == OBJ; }
    size_t size() const { return type == ARR ? items.size() : type == OBJ ? members.size() : 0; }
    bool has(const char* key) const {
        for (const auto& m : members)
            if (m.first == key) return true;
        return false;
    }
    const Value& operator[](const char* key) const {
        if (type == OBJ)
            for (const auto& m : members)
                if (m.first == key) return m.second;
        return null();
    }
    const Value& operator[](size_t i) const { return type == ARR && i < items.size() ? items[i] : null(); }
    const Value& operator[](int i) const { return (*this)[(size_t)(i < 0 ? items.size() : (size_t)i)]; }

    // The value as text: strings as they are, numbers written out, else `def`.
    std::string str(const std::string& def = "") const {
        if (type == STR) return s;
        if (type == NUM) {
            char buf[32];
            if (n == (double)(long long)n && n < 1e15 && n > -1e15) snprintf(buf, sizeof(buf), "%lld", (long long)n);
            else snprintf(buf, sizeof(buf), "%.10g", n);
            return buf;
        }
        if (type == BOOL) return b ? "true" : "false";
        return def;
    }
    // The value as a number; numeric strings ("14074.5") count too.
    double num(double def = 0) const {
        if (type == NUM) return n;
        if (type == STR && !s.empty()) {
            char* end = nullptr;
            double v = strtod(s.c_str(), &end);
            if (end && *end == 0) return v;
        }
        if (type == BOOL) return b ? 1 : 0;
        return def;
    }
    bool flag(bool def = false) const {
        if (type == BOOL) return b;
        if (type == NUM) return n != 0;
        if (type == STR) return s == "true" || s == "1";
        return def;
    }
};

class Parser {
public:
    Parser(const char* p, const char* end) : p_(p), end_(end) {}

    bool document(Value& out) {
        if (!value(out, 0)) return false;
        ws();
        return p_ == end_;
    }

private:
    const char* p_;
    const char* end_;
    static constexpr int kMaxDepth = 32;

    void ws() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) p_++;
    }
    bool lit(const char* w) {
        const char* q = p_;
        for (; *w; w++, q++)
            if (q >= end_ || *q != *w) return false;
        p_ = q;
        return true;
    }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += (char)c;
        } else if (c < 0x800) {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xF0 | (c >> 18));
            out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    bool hex4(uint32_t& v) {
        if (end_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (p_ >= end_ || *p_ != '"') return false;
        p_++;
        while (p_ < end_) {
            char c = *p_++;
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= end_) return false;
            char e = *p_++;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t c1;
                    if (!hex4(c1)) return false;
                    if (c1 >= 0xD800 && c1 < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                        p_ += 2;
                        uint32_t c2;
                        if (!hex4(c2)) return false;
                        if (c2 >= 0xDC00 && c2 < 0xE000) c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                    }
                    utf8(out, c1);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Value& v, int depth) {
        if (depth > kMaxDepth) return false;
        ws();
        if (p_ >= end_) return false;
        char c = *p_;
        if (c == '{') {
            p_++;
            v.type = Value::OBJ;
            ws();
            if (p_ < end_ && *p_ == '}') { p_++; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (p_ >= end_ || *p_ != ':') return false;
                p_++;
                v.members.emplace_back(std::move(key), Value());
                if (!value(v.members.back().second, depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') { p_++; continue; }
                if (p_ < end_ && *p_ == '}') { p_++; return true; }
                return false;
            }
        }
        if (c == '[') {
            p_++;
            v.type = Value::ARR;
            ws();
            if (p_ < end_ && *p_ == ']') { p_++; return true; }
            for (;;) {
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') { p_++; continue; }
                if (p_ < end_ && *p_ == ']') { p_++; return true; }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::STR;
            return string(v.s);
        }
        if (lit("true")) { v.type = Value::BOOL; v.b = true; return true; }
        if (lit("false")) { v.type = Value::BOOL; v.b = false; return true; }
        if (lit("null")) { v.type = Value::NUL; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = p_;
            while (p_ < end_ && (strchr("+-.eE", *p_) || (*p_ >= '0' && *p_ <= '9'))) p_++;
            std::string num(start, p_);
            char* e = nullptr;
            v.type = Value::NUM;
            v.n = strtod(num.c_str(), &e);
            return e && *e == 0;
        }
        return false;
    }
};

inline bool parse(const std::string& text, Value& out) {
    out = Value();
    Parser p(text.data(), text.data() + text.size());
    return p.document(out);
}

}  // namespace jdom
