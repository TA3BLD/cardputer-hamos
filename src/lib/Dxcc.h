// Callsign prefix -> country. A compact built-in table of the commonly heard
// entities (not the full DXCC list; rare prefixes return nullptr) with an
// approximate centre used for beam headings. Pure C++, host-testable.
#pragma once
#include <cctype>
#include <cstring>
#include <string>

namespace dxcc {

struct Entity {
    const char* name;
    float lat, lon;
    const char* prefixes;   // space separated; the longest matching prefix wins
    const char* iso;        // flag file name: ISO 3166 code ("tr", "gb-sct")
};

inline const Entity* table(size_t& count) {
    static const Entity e[] = {
        // --- Europe ---
        {"Turkey", 39, 35, "TA TB TC YM", "tr"},
        {"Germany", 51, 10, "DA DB DC DD DE DF DG DH DI DJ DK DL DM DN DO DP DQ DR", "de"},
        {"England", 52.5f, -1.5f, "G M 2E", "gb-eng"},
        {"Scotland", 57, -4, "GM GS MM MS 2M", "gb-sct"},
        {"Wales", 52.3f, -3.7f, "GW GC MW MC 2W", "gb-wls"},
        {"N. Ireland", 54.6f, -6.7f, "GI GN MI MN 2I", "gb-nir"},
        {"Isle of Man", 54.2f, -4.5f, "GD GT MD MT 2D", "im"},
        {"Jersey", 49.2f, -2.1f, "GJ GH MJ MH 2J", "je"},
        {"Guernsey", 49.5f, -2.6f, "GU GP MU MP 2U", "gg"},
        {"Ireland", 53.2f, -8, "EI EJ", "ie"},
        {"France", 46.5f, 2.5f, "F TM", "fr"},
        {"Corsica", 42, 9, "TK", "fr"},
        {"Italy", 42.5f, 12.5f, "I", "it"},
        {"Sardinia", 40, 9, "IS0 IM0", "it"},
        {"Spain", 40, -3.7f, "EA EB EC ED EE EF EG EH AM AN AO", "es"},
        {"Balearic Is.", 39.6f, 3, "EA6 EB6 EC6 ED6 EE6 EF6", "es"},
        {"Canary Is.", 28.3f, -15.8f, "EA8 EB8 EC8 ED8 EE8 EF8", "es"},
        {"Ceuta & Melilla", 35.6f, -4, "EA9 EB9 EC9 ED9 EE9 EF9", "es"},
        {"Portugal", 39.5f, -8, "CT CQ CR CS", "pt"},
        {"Madeira", 32.7f, -17, "CT3 CT9 CQ3 CQ9", "pt"},
        {"Azores", 38.5f, -28, "CU", "pt"},
        {"Netherlands", 52.2f, 5.5f, "PA PB PC PD PE PF PG PH PI", "nl"},
        {"Belgium", 50.6f, 4.6f, "ON OO OP OQ OR OS OT", "be"},
        {"Luxembourg", 49.7f, 6.1f, "LX", "lu"},
        {"Switzerland", 46.8f, 8.2f, "HB HE", "ch"},
        {"Liechtenstein", 47.1f, 9.5f, "HB0", "li"},
        {"Austria", 47.5f, 14, "OE", "at"},
        {"Czech Rep.", 49.8f, 15.5f, "OK OL", "cz"},
        {"Slovakia", 48.7f, 19.5f, "OM", "sk"},
        {"Poland", 52, 19, "SN SO SP SQ SR 3Z HF", "pl"},
        {"Hungary", 47, 19.5f, "HA HG", "hu"},
        {"Romania", 46, 25, "YO YP YQ YR", "ro"},
        {"Bulgaria", 42.7f, 25.3f, "LZ", "bg"},
        {"Greece", 39, 22, "SV SW SX SY SZ J4", "gr"},
        {"Dodecanese", 36.4f, 28, "SV5 SW5 SX5 J45", "gr"},
        {"Crete", 35.2f, 25, "SV9 SW9 SX9 J49", "gr"},
        {"Cyprus", 35, 33, "5B C4 H2 P3", "cy"},
        {"Serbia", 44, 21, "YT YU", "rs"},
        {"Croatia", 45, 16, "9A", "hr"},
        {"Slovenia", 46, 15, "S5", "si"},
        {"Bosnia-Herz.", 44, 18, "E7", "ba"},
        {"Montenegro", 42.7f, 19.3f, "4O", "me"},
        {"N. Macedonia", 41.6f, 21.7f, "Z3", "mk"},
        {"Albania", 41, 20, "ZA", "al"},
        {"Kosovo", 42.6f, 21, "Z6", "xk"},
        {"Ukraine", 49, 32, "UR US UT UU UV UW UX UY UZ EM EN EO", "ua"},
        {"Belarus", 53.5f, 28, "EU EV EW", "by"},
        {"Moldova", 47.2f, 28.5f, "ER", "md"},
        {"Lithuania", 55.3f, 24, "LY", "lt"},
        {"Latvia", 57, 25, "YL", "lv"},
        {"Estonia", 58.7f, 25.5f, "ES", "ee"},
        {"European Russia", 55.7f, 37.6f, "R U", "ru"},
        {"Kaliningrad", 54.7f, 20.5f, "UA2 RA2 R2F RK2F", "ru"},
        {"Finland", 62, 26, "OF OG OH OI OJ", "fi"},
        {"Aland Is.", 60.2f, 20, "OH0 OF0 OG0", "ax"},
        {"Sweden", 60, 15, "SA SB SC SD SE SF SG SH SI SJ SK SL SM 7S 8S", "se"},
        {"Norway", 61, 9, "LA LB LC LD LE LF LG LH LI LJ LK LL LM LN", "no"},
        {"Svalbard", 78, 16, "JW", "sj"},
        {"Denmark", 56, 10, "OU OV OW OZ 5P 5Q", "dk"},
        {"Faroe Is.", 62, -7, "OY", "fo"},
        {"Greenland", 70, -42, "OX", "gl"},
        {"Iceland", 65, -18, "TF", "is"},
        {"Malta", 35.9f, 14.4f, "9H", "mt"},
        {"San Marino", 43.9f, 12.5f, "T7", "sm"},
        {"Vatican", 41.9f, 12.45f, "HV", "va"},
        {"Monaco", 43.7f, 7.4f, "3A", "mc"},
        {"Andorra", 42.5f, 1.5f, "C3", "ad"},
        {"Gibraltar", 36.1f, -5.3f, "ZB", "gi"},
        // --- Asia ---
        {"Asiatic Russia", 60, 95, "UA8 UA9 UA0 RA8 RA9 RA0 R8 R9 R0", "ru"},
        {"Kazakhstan", 48, 68, "UN UO UP UQ", "kz"},
        {"Uzbekistan", 41, 64, "UJ UK UL UM", "uz"},
        {"Kyrgyzstan", 41.5f, 75, "EX", "kg"},
        {"Tajikistan", 38.8f, 71, "EY", "tj"},
        {"Turkmenistan", 39, 60, "EZ", "tm"},
        {"Georgia", 42, 43.5f, "4L", "ge"},
        {"Armenia", 40.3f, 45, "EK", "am"},
        {"Azerbaijan", 40.4f, 48, "4J 4K", "az"},
        {"Israel", 31.5f, 35, "4X 4Z", "il"},
        {"Lebanon", 33.9f, 35.8f, "OD", "lb"},
        {"Jordan", 31, 36.5f, "JY", "jo"},
        {"Syria", 35, 38.5f, "YK", "sy"},
        {"Iraq", 33, 44, "YI", "iq"},
        {"Iran", 32, 53, "EP EQ", "ir"},
        {"Saudi Arabia", 24, 45, "HZ 7Z 8Z", "sa"},
        {"UAE", 24, 54, "A6", "ae"},
        {"Qatar", 25.3f, 51.2f, "A7", "qa"},
        {"Bahrain", 26, 50.5f, "A9", "bh"},
        {"Kuwait", 29.3f, 47.7f, "9K", "kw"},
        {"Oman", 21, 57, "A4", "om"},
        {"India", 22, 79, "VU AT AU AV AW", "in"},
        {"Pakistan", 30, 70, "AP AQ AR AS", "pk"},
        {"Sri Lanka", 7.5f, 80.7f, "4S", "lk"},
        {"Nepal", 28, 84, "9N", "np"},
        {"Bangladesh", 24, 90, "S2", "bd"},
        {"China", 35, 105, "B", "cn"},
        {"Taiwan", 23.7f, 121, "BM BN BO BP BQ BU BV BW BX", "tw"},
        {"Hong Kong", 22.3f, 114.2f, "VR", "hk"},
        {"Japan", 36, 138, "JA JE JF JG JH JI JJ JK JL JM JN JO JP JQ JR JS 7J 7K 7L 7M 7N 8J 8N", "jp"},
        {"South Korea", 36.5f, 127.8f, "HL DS DT 6K 6L 6M 6N D7 D8 D9", "kr"},
        {"Thailand", 15, 101, "HS E2", "th"},
        {"Vietnam", 16, 107, "XV 3W", "vn"},
        {"West Malaysia", 4, 102, "9M2 9M4 9W2 9W4", "my"},
        {"East Malaysia", 4, 115, "9M6 9M8 9W6 9W8", "my"},
        {"Singapore", 1.35f, 103.8f, "9V", "sg"},
        {"Indonesia", -2, 118, "YB YC YD YE YF YG YH", "id"},
        {"Philippines", 13, 122, "DU DV DW DX DY DZ 4D 4E 4F 4G 4H 4I", "ph"},
        // --- Oceania ---
        {"Australia", -25, 134, "VK AX", "au"},
        {"New Zealand", -41, 174, "ZL ZM", "nz"},
        {"Hawaii", 20.8f, -157, "KH6 KH7 AH6 AH7 NH6 NH7 WH6 WH7", "us"},
        {"Guam", 13.5f, 144.8f, "KH2 AH2 NH2 WH2", "gu"},
        {"New Caledonia", -21.5f, 165.5f, "FK", "nc"},
        {"Fr. Polynesia", -17.6f, -149.5f, "FO", "pf"},
        // --- North America ---
        {"USA", 39, -98, "K W N AA AB AC AD AE AF AG AH AI AJ AK AL", "us"},
        {"Alaska", 64, -150, "KL AL NL WL", "us"},
        {"Puerto Rico", 18.2f, -66.5f, "KP3 KP4 NP3 NP4 WP3 WP4", "pr"},
        {"US Virgin Is.", 18, -64.8f, "KP2 NP2 WP2", "vi"},
        {"Canada", 50, -95, "VA VB VC VD VE VF VG VO VY CF CG CH CI CJ CK CY CZ", "ca"},
        {"Mexico", 23, -102, "XA XB XC XD XE XF XG XH XI 4A 4B 4C", "mx"},
        {"Cuba", 22, -79, "CO CM T4", "cu"},
        {"Dominican Rep.", 19, -70, "HI", "do"},
        {"Jamaica", 18.1f, -77.3f, "6Y", "jm"},
        {"Bahamas", 24.5f, -77, "C6", "bs"},
        {"Guatemala", 15.5f, -90.3f, "TG TD", "gt"},
        {"Costa Rica", 10, -84, "TI TE", "cr"},
        {"Panama", 9, -80, "HP HO H3 H8 H9", "pa"},
        {"Guadeloupe", 16.2f, -61.6f, "FG", "gp"},
        {"Martinique", 14.6f, -61, "FM", "mq"},
        {"St. Pierre & Miq.", 46.8f, -56.3f, "FP", "pm"},
        // --- South America ---
        {"Brazil", -10, -52, "PP PQ PR PS PT PU PV PW PX PY ZV ZW ZX ZY ZZ", "br"},
        {"Argentina", -35, -64, "LO LP LQ LR LS LT LU LV LW AY AZ", "ar"},
        {"Chile", -33, -71, "CA CB CC CD CE XQ XR 3G", "cl"},
        {"Uruguay", -33, -56, "CV CW CX", "uy"},
        {"Paraguay", -23, -58, "ZP", "py"},
        {"Peru", -10, -76, "OA OB OC", "pe"},
        {"Bolivia", -17, -65, "CP", "bo"},
        {"Colombia", 4, -73, "HJ HK 5J 5K", "co"},
        {"Venezuela", 8, -66, "YV YW YX YY 4M", "ve"},
        {"Ecuador", -1.5f, -78.5f, "HC HD", "ec"},
        {"French Guiana", 4, -53, "FY", "gf"},
        // --- Africa ---
        {"Egypt", 27, 30, "SU", "eg"},
        {"Libya", 27, 17, "5A", "ly"},
        {"Tunisia", 34, 9, "3V TS", "tn"},
        {"Algeria", 28, 3, "7X 7T 7U 7V 7W 7Y", "dz"},
        {"Morocco", 32, -6, "CN", "ma"},
        {"Cape Verde", 15, -24, "D4", "cv"},
        {"Senegal", 14.5f, -14.5f, "6W 6V", "sn"},
        {"Ghana", 8, -1, "9G", "gh"},
        {"Nigeria", 9, 8, "5N 5O", "ng"},
        {"Ethiopia", 9, 39, "ET", "et"},
        {"Kenya", 0, 38, "5Z 5Y", "ke"},
        {"Tanzania", -6, 35, "5H 5I", "tz"},
        {"Namibia", -22, 17, "V5", "na"},
        {"South Africa", -30, 25, "ZR ZS ZT ZU", "za"},
        {"Madagascar", -19, 47, "5R 5S", "mg"},
        {"Reunion", -21.1f, 55.5f, "FR", "re"},
        {"Mayotte", -12.8f, 45.2f, "FH", "yt"},
        {"Mauritius", -20.3f, 57.6f, "3B8", "mu"},
    };
    count = sizeof(e) / sizeof(e[0]);
    return e;
}

// The part of a callsign that carries the prefix: "DL/TA3BLD" -> "DL",
// "TA3BLD/P" -> "TA3BLD", "EA8/DL1ABC/P" -> "EA8".
inline std::string prefixPart(const std::string& call) {
    std::string best, token;
    for (size_t i = 0; i <= call.size(); i++) {
        if (i < call.size() && call[i] != '/') {
            token += (char)toupper((unsigned char)call[i]);
            continue;
        }
        bool modifier = token.empty() || token == "P" || token == "M" || token == "MM" || token == "AM" ||
                        token == "QRP" || token == "A" || token == "B" ||
                        (token.size() == 1 && isdigit((unsigned char)token[0]));
        if (!modifier && (best.empty() || token.size() < best.size())) best = token;
        token.clear();
    }
    return best;
}

inline const Entity* lookup(const std::string& call) {
    std::string c = prefixPart(call);
    if (c.empty()) return nullptr;
    size_t count;
    const Entity* e = table(count);
    const Entity* best = nullptr;
    size_t bestLen = 0;
    for (size_t i = 0; i < count; i++) {
        const char* p = e[i].prefixes;
        while (*p) {
            const char* end = strchr(p, ' ');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len > bestLen && len <= c.size() && c.compare(0, len, p, len) == 0) {
                best = &e[i];
                bestLen = len;
            }
            p += len;
            while (*p == ' ') p++;
        }
    }
    return best;
}

}  // namespace dxcc
