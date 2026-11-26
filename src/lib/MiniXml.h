// Just enough XML for web APIs that answer in XML (HamQTH, QRZ, PSKReporter):
// the text of the first <tag>, and the attributes of each <element .../>.
// No DTDs, namespaces or CDATA.
#pragma once
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace xml {

inline std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t e = s.find(';', i);
        if (e == std::string::npos || e - i > 10) {
            out += s[i];
            continue;
        }
        std::string ent = s.substr(i + 1, e - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long c = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                 : strtoul(ent.c_str() + 1, nullptr, 10);
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        } else {
            out += s.substr(i, e - i + 1);
        }
        i = e;
    }
    return out;
}

// Text inside the first <tag>...</tag> (the tag name matched exactly,
// ignoring letter case), "" if missing.
inline std::string text(const std::string& doc, const std::string& tag, size_t from = 0) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)tolower((unsigned char)c);
        return s;
    };
    std::string want = lower(tag);
    size_t p = from;
    while ((p = doc.find('<', p)) != std::string::npos) {
        size_t e = doc.find_first_of(" >/", p + 1);
        if (e == std::string::npos) return "";
        if (lower(doc.substr(p + 1, e - p - 1)) == want) {
            size_t close = doc.find('>', p);
            if (close == std::string::npos || doc[close - 1] == '/') return "";
            size_t end = doc.find("</", close);
            if (end == std::string::npos) return "";
            return unescape(doc.substr(close + 1, end - close - 1));
        }
        p = e;
    }
    return "";
}

using Attrs = std::map<std::string, std::string>;

// Attributes of every <name ...> element.
inline std::vector<Attrs> elements(const std::string& doc, const std::string& name) {
    std::vector<Attrs> out;
    std::string open = "<" + name;
    size_t p = 0;
    while ((p = doc.find(open, p)) != std::string::npos) {
        size_t q = p + open.size();
        p = q;
        if (q >= doc.size() || (doc[q] != ' ' && doc[q] != '/' && doc[q] != '>' && doc[q] != '\n' && doc[q] != '\t')) continue;
        size_t end = doc.find('>', q);
        if (end == std::string::npos) break;
        Attrs a;
        size_t i = q;
        while (i < end) {
            while (i < end && isspace((unsigned char)doc[i])) i++;
            size_t eq = doc.find('=', i);
            if (eq == std::string::npos || eq > end) break;
            std::string key = doc.substr(i, eq - i);
            while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
            size_t v = eq + 1;
            while (v < end && isspace((unsigned char)doc[v])) v++;
            if (v >= end || (doc[v] != '"' && doc[v] != '\'')) break;
            char quote = doc[v];
            size_t ve = doc.find(quote, v + 1);
            if (ve == std::string::npos || ve > end) break;
            a[key] = unescape(doc.substr(v + 1, ve - v - 1));
            i = ve + 1;
        }
        out.push_back(a);
        p = end;
    }
    return out;
}

}  // namespace xml
