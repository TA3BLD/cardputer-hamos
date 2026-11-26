// Award progress from the logbook: DXCC countries (from the built-in table),
// continents (WAC), the 81 provinces of Turkey (from the town in the contact:
// callbook QTH or the note), and grid squares. Worked, not confirmed.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Adif.h"
#include "Dxcc.h"
#include "Emergency.h"

namespace awards {

// Provinces in licence-plate order (index + 1 = plate code).
inline const char* const* provinces() {
    static const char* const p[81] = {
        "Adana", "Adıyaman", "Afyonkarahisar", "Ağrı", "Amasya", "Ankara", "Antalya", "Artvin", "Aydın", "Balıkesir",
        "Bilecik", "Bingöl", "Bitlis", "Bolu", "Burdur", "Bursa", "Çanakkale", "Çankırı", "Çorum", "Denizli",
        "Diyarbakır", "Edirne", "Elazığ", "Erzincan", "Erzurum", "Eskişehir", "Gaziantep", "Giresun", "Gümüşhane", "Hakkari",
        "Hatay", "Isparta", "Mersin", "İstanbul", "İzmir", "Kars", "Kastamonu", "Kayseri", "Kırklareli", "Kırşehir",
        "Kocaeli", "Konya", "Kütahya", "Malatya", "Manisa", "Kahramanmaraş", "Mardin", "Muğla", "Muş", "Nevşehir",
        "Niğde", "Ordu", "Rize", "Sakarya", "Samsun", "Siirt", "Sinop", "Sivas", "Tekirdağ", "Tokat",
        "Trabzon", "Tunceli", "Şanlıurfa", "Uşak", "Van", "Yozgat", "Zonguldak", "Aksaray", "Bayburt", "Karaman",
        "Kırıkkale", "Batman", "Şırnak", "Bartın", "Ardahan", "Iğdır", "Yalova", "Karabük", "Kilis", "Osmaniye", "Düzce"};
    return p;
}
constexpr int kProvinces = 81;

// Province (0..80) named in a text such as "Karşıyaka, İzmir" or "IZMIR", -1 if none.
inline int provinceOf(const std::string& text) {
    std::string t = sos::fold(text);
    auto hasWord = [&](const std::string& w) {
        size_t p = 0;
        while ((p = t.find(w, p)) != std::string::npos) {
            bool b = p == 0 || !isalnum((unsigned char)t[p - 1]);
            size_t e = p + w.size();
            bool a = e >= t.size() || !isalnum((unsigned char)t[e]);
            if (b && a) return true;
            p = e;
        }
        return false;
    };
    for (int i = 0; i < kProvinces; i++)
        if (hasWord(sos::fold(provinces()[i]))) return i;
    // Common other names
    static const struct { const char* name; int index; } alias[] = {
        {"AFYON", 2}, {"ICEL", 32}, {"ANTEP", 26}, {"MARAS", 45}, {"URFA", 62}, {"IZMIT", 40}, {"ADAPAZARI", 53},
        {"ANTAKYA", 30}, {"DERSIM", 61}};
    for (const auto& a : alias)
        if (hasWord(a.name)) return a.index;
    return -1;
}

struct Tally {
    std::set<const dxcc::Entity*> countries;
    std::map<std::string, std::set<const dxcc::Entity*>> countriesByBand;
    std::set<std::string> continents;
    std::set<int> provinceSet;
    std::set<std::string> grids;                        // 4 characters, upper case
    std::map<std::string, std::set<std::string>> gridsByBand;
    size_t contacts = 0;

    void add(const adif::Qso& q) {
        contacts++;
        std::string band = adif::band(atof(q.freq.c_str()));
        const dxcc::Entity* e = dxcc::lookup(q.call);
        if (e) {
            countries.insert(e);
            if (!band.empty()) countriesByBand[band].insert(e);
            if (e->continent) continents.insert(e->continent);
            if (std::string(e->name) == "Turkey") {
                int p = provinceOf(q.qth);
                if (p < 0) p = provinceOf(q.comment);
                if (p >= 0) provinceSet.insert(p);
            }
        }
        if (q.grid.size() >= 4) {
            std::string g = q.grid.substr(0, 4);
            for (char& c : g) c = (char)toupper((unsigned char)c);
            if (g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' && isdigit((unsigned char)g[2]) && isdigit((unsigned char)g[3])) {
                grids.insert(g);
                if (!band.empty()) gridsByBand[band].insert(g);
            }
        }
    }
};

}  // namespace awards
