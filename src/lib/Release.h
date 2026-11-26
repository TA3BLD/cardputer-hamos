// Reading a GitHub release (GET /repos/{owner}/{repo}/releases/latest) and
// comparing version numbers, for the update check.
#pragma once
#include <cctype>
#include <cstdlib>
#include <string>
#include "JsonDom.h"

namespace release {

// "v1.2.10" vs "1.2.9": < 0, 0 or > 0 like strcmp. Missing parts count as 0;
// anything after a '-' (1.0.0-beta) makes it older than the same plain version.
inline int compareVersions(const std::string& a, const std::string& b) {
    auto strip = [](const std::string& s) { return !s.empty() && (s[0] == 'v' || s[0] == 'V') ? s.substr(1) : s; };
    std::string x = strip(a), y = strip(b);
    size_t i = 0, j = 0;
    for (int part = 0; part < 4; part++) {
        long u = 0, v = 0;
        while (i < x.size() && isdigit((unsigned char)x[i])) u = u * 10 + (x[i++] - '0');
        while (j < y.size() && isdigit((unsigned char)y[j])) v = v * 10 + (y[j++] - '0');
        if (u != v) return u < v ? -1 : 1;
        if (i < x.size() && x[i] == '.') i++;
        if (j < y.size() && y[j] == '.') j++;
    }
    bool preX = i < x.size() && x[i] == '-', preY = j < y.size() && y[j] == '-';
    if (preX != preY) return preX ? -1 : 1;
    return 0;
}

struct Info {
    std::string version;        // "0.9.0", without the "v"
    std::string name, notes;    // release title and text
    std::string assetApiUrl;    // https://api.github.com/repos/.../releases/assets/123 (works for private repos)
    std::string downloadUrl;    // https://github.com/.../releases/download/v0.9.0/firmware.bin
    std::string sha256;         // hex, from the asset's "digest" when GitHub gives one
    size_t size = 0;
    bool prerelease = false;
};

// Picks the asset called `assetName` (firmware.bin). False if the text is not
// a release or has no such asset.
inline bool parse(const std::string& json, Info& out, const char* assetName = "firmware.bin") {
    jdom::Value v;
    if (!jdom::parse(json, v) || !v.isObject()) return false;
    std::string tag = v["tag_name"].str();
    if (tag.empty()) return false;
    out = Info();
    out.version = (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
    out.name = v["name"].str();
    out.notes = v["body"].str();
    out.prerelease = v["prerelease"].flag();
    const jdom::Value& assets = v["assets"];
    for (size_t i = 0; i < assets.size(); i++) {
        const jdom::Value& a = assets[i];
        if (a["name"].str() != assetName) continue;
        out.assetApiUrl = a["url"].str();
        out.downloadUrl = a["browser_download_url"].str();
        out.size = (size_t)a["size"].num();
        std::string d = a["digest"].str();
        if (d.rfind("sha256:", 0) == 0 && d.size() == 7 + 64) out.sha256 = d.substr(7);
        return !out.downloadUrl.empty() || !out.assetApiUrl.empty();
    }
    return false;
}

}  // namespace release
