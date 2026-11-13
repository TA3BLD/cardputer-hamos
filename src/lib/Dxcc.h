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
};

inline const Entity* table(size_t& count) {
    static const Entity e[] = {
        // --- Europe ---
        {"Turkey", 39, 35, "TA TB TC YM"},
        {"Germany", 51, 10, "DA DB DC DD DE DF DG DH DI DJ DK DL DM DN DO DP DQ DR"},
        {"England", 52.5f, -1.5f, "G M 2E"},
        {"Scotland", 57, -4, "GM GS MM MS 2M"},
        {"Wales", 52.3f, -3.7f, "GW GC MW MC 2W"},
        {"N. Ireland", 54.6f, -6.7f, "GI GN MI MN 2I"},
        {"Isle of Man", 54.2f, -4.5f, "GD GT MD MT 2D"},
        {"Jersey", 49.2f, -2.1f, "GJ GH MJ MH 2J"},
        {"Guernsey", 49.5f, -2.6f, "GU GP MU MP 2U"},
        {"Ireland", 53.2f, -8, "EI EJ"},
        {"France", 46.5f, 2.5f, "F TM"},
        {"Corsica", 42, 9, "TK"},
        {"Italy", 42.5f, 12.5f, "I"},
        {"Sardinia", 40, 9, "IS0 IM0"},
        {"Spain", 40, -3.7f, "EA EB EC ED EE EF EG EH AM AN AO"},
        {"Balearic Is.", 39.6f, 3, "EA6 EB6 EC6 ED6 EE6 EF6"},
        {"Canary Is.", 28.3f, -15.8f, "EA8 EB8 EC8 ED8 EE8 EF8"},
        {"Ceuta & Melilla", 35.6f, -4, "EA9 EB9 EC9 ED9 EE9 EF9"},
        {"Portugal", 39.5f, -8, "CT CQ CR CS"},
        {"Madeira", 32.7f, -17, "CT3 CT9 CQ3 CQ9"},
        {"Azores", 38.5f, -28, "CU"},
        {"Netherlands", 52.2f, 5.5f, "PA PB PC PD PE PF PG PH PI"},
        {"Belgium", 50.6f, 4.6f, "ON OO OP OQ OR OS OT"},
        {"Luxembourg", 49.7f, 6.1f, "LX"},
        {"Switzerland", 46.8f, 8.2f, "HB HE"},
        {"Liechtenstein", 47.1f, 9.5f, "HB0"},
        {"Austria", 47.5f, 14, "OE"},
        {"Czech Rep.", 49.8f, 15.5f, "OK OL"},
        {"Slovakia", 48.7f, 19.5f, "OM"},
        {"Poland", 52, 19, "SN SO SP SQ SR 3Z HF"},
        {"Hungary", 47, 19.5f, "HA HG"},
        {"Romania", 46, 25, "YO YP YQ YR"},
        {"Bulgaria", 42.7f, 25.3f, "LZ"},
        {"Greece", 39, 22, "SV SW SX SY SZ J4"},
        {"Dodecanese", 36.4f, 28, "SV5 SW5 SX5 J45"},
        {"Crete", 35.2f, 25, "SV9 SW9 SX9 J49"},
        {"Cyprus", 35, 33, "5B C4 H2 P3"},
        {"Serbia", 44, 21, "YT YU"},
        {"Croatia", 45, 16, "9A"},
        {"Slovenia", 46, 15, "S5"},
        {"Bosnia-Herz.", 44, 18, "E7"},
        {"Montenegro", 42.7f, 19.3f, "4O"},
        {"N. Macedonia", 41.6f, 21.7f, "Z3"},
        {"Albania", 41, 20, "ZA"},
        {"Kosovo", 42.6f, 21, "Z6"},
        {"Ukraine", 49, 32, "UR US UT UU UV UW UX UY UZ EM EN EO"},
        {"Belarus", 53.5f, 28, "EU EV EW"},
        {"Moldova", 47.2f, 28.5f, "ER"},
        {"Lithuania", 55.3f, 24, "LY"},
        {"Latvia", 57, 25, "YL"},
        {"Estonia", 58.7f, 25.5f, "ES"},
        {"European Russia", 55.7f, 37.6f, "R U"},
        {"Kaliningrad", 54.7f, 20.5f, "UA2 RA2 R2F RK2F"},
        {"Finland", 62, 26, "OF OG OH OI OJ"},
        {"Aland Is.", 60.2f, 20, "OH0 OF0 OG0"},
        {"Sweden", 60, 15, "SA SB SC SD SE SF SG SH SI SJ SK SL SM 7S 8S"},
        {"Norway", 61, 9, "LA LB LC LD LE LF LG LH LI LJ LK LL LM LN"},
        {"Svalbard", 78, 16, "JW"},
        {"Denmark", 56, 10, "OU OV OW OZ 5P 5Q"},
        {"Faroe Is.", 62, -7, "OY"},
        {"Greenland", 70, -42, "OX"},
        {"Iceland", 65, -18, "TF"},
        {"Malta", 35.9f, 14.4f, "9H"},
        {"San Marino", 43.9f, 12.5f, "T7"},
        {"Vatican", 41.9f, 12.45f, "HV"},
        {"Monaco", 43.7f, 7.4f, "3A"},
        {"Andorra", 42.5f, 1.5f, "C3"},
        {"Gibraltar", 36.1f, -5.3f, "ZB"},
        // --- Asia ---
        {"Asiatic Russia", 60, 95, "UA8 UA9 UA0 RA8 RA9 RA0 R8 R9 R0"},
        {"Kazakhstan", 48, 68, "UN UO UP UQ"},
        {"Uzbekistan", 41, 64, "UJ UK UL UM"},
        {"Kyrgyzstan", 41.5f, 75, "EX"},
        {"Tajikistan", 38.8f, 71, "EY"},
        {"Turkmenistan", 39, 60, "EZ"},
        {"Georgia", 42, 43.5f, "4L"},
        {"Armenia", 40.3f, 45, "EK"},
        {"Azerbaijan", 40.4f, 48, "4J 4K"},
        {"Israel", 31.5f, 35, "4X 4Z"},
        {"Lebanon", 33.9f, 35.8f, "OD"},
        {"Jordan", 31, 36.5f, "JY"},
        {"Syria", 35, 38.5f, "YK"},
        {"Iraq", 33, 44, "YI"},
        {"Iran", 32, 53, "EP EQ"},
        {"Saudi Arabia", 24, 45, "HZ 7Z 8Z"},
        {"UAE", 24, 54, "A6"},
        {"Qatar", 25.3f, 51.2f, "A7"},
        {"Bahrain", 26, 50.5f, "A9"},
        {"Kuwait", 29.3f, 47.7f, "9K"},
        {"Oman", 21, 57, "A4"},
        {"India", 22, 79, "VU AT AU AV AW"},
        {"Pakistan", 30, 70, "AP AQ AR AS"},
        {"Sri Lanka", 7.5f, 80.7f, "4S"},
        {"Nepal", 28, 84, "9N"},
        {"Bangladesh", 24, 90, "S2"},
        {"China", 35, 105, "B"},
        {"Taiwan", 23.7f, 121, "BM BN BO BP BQ BU BV BW BX"},
        {"Hong Kong", 22.3f, 114.2f, "VR"},
        {"Japan", 36, 138, "JA JE JF JG JH JI JJ JK JL JM JN JO JP JQ JR JS 7J 7K 7L 7M 7N 8J 8N"},
        {"South Korea", 36.5f, 127.8f, "HL DS DT 6K 6L 6M 6N D7 D8 D9"},
        {"Thailand", 15, 101, "HS E2"},
        {"Vietnam", 16, 107, "XV 3W"},
        {"West Malaysia", 4, 102, "9M2 9M4 9W2 9W4"},
        {"East Malaysia", 4, 115, "9M6 9M8 9W6 9W8"},
        {"Singapore", 1.35f, 103.8f, "9V"},
        {"Indonesia", -2, 118, "YB YC YD YE YF YG YH"},
        {"Philippines", 13, 122, "DU DV DW DX DY DZ 4D 4E 4F 4G 4H 4I"},
        // --- Oceania ---
        {"Australia", -25, 134, "VK AX"},
        {"New Zealand", -41, 174, "ZL ZM"},
        {"Hawaii", 20.8f, -157, "KH6 KH7 AH6 AH7 NH6 NH7 WH6 WH7"},
        {"Guam", 13.5f, 144.8f, "KH2 AH2 NH2 WH2"},
        {"New Caledonia", -21.5f, 165.5f, "FK"},
        {"Fr. Polynesia", -17.6f, -149.5f, "FO"},
        // --- North America ---
        {"USA", 39, -98, "K W N AA AB AC AD AE AF AG AH AI AJ AK AL"},
        {"Alaska", 64, -150, "KL AL NL WL"},
        {"Puerto Rico", 18.2f, -66.5f, "KP3 KP4 NP3 NP4 WP3 WP4"},
        {"US Virgin Is.", 18, -64.8f, "KP2 NP2 WP2"},
        {"Canada", 50, -95, "VA VB VC VD VE VF VG VO VY CF CG CH CI CJ CK CY CZ"},
        {"Mexico", 23, -102, "XA XB XC XD XE XF XG XH XI 4A 4B 4C"},
        {"Cuba", 22, -79, "CO CM T4"},
        {"Dominican Rep.", 19, -70, "HI"},
        {"Jamaica", 18.1f, -77.3f, "6Y"},
        {"Bahamas", 24.5f, -77, "C6"},
        {"Guatemala", 15.5f, -90.3f, "TG TD"},
        {"Costa Rica", 10, -84, "TI TE"},
        {"Panama", 9, -80, "HP HO H3 H8 H9"},
        {"Guadeloupe", 16.2f, -61.6f, "FG"},
        {"Martinique", 14.6f, -61, "FM"},
        {"St. Pierre & Miq.", 46.8f, -56.3f, "FP"},
        // --- South America ---
        {"Brazil", -10, -52, "PP PQ PR PS PT PU PV PW PX PY ZV ZW ZX ZY ZZ"},
        {"Argentina", -35, -64, "LO LP LQ LR LS LT LU LV LW AY AZ"},
        {"Chile", -33, -71, "CA CB CC CD CE XQ XR 3G"},
        {"Uruguay", -33, -56, "CV CW CX"},
        {"Paraguay", -23, -58, "ZP"},
        {"Peru", -10, -76, "OA OB OC"},
        {"Bolivia", -17, -65, "CP"},
        {"Colombia", 4, -73, "HJ HK 5J 5K"},
        {"Venezuela", 8, -66, "YV YW YX YY 4M"},
        {"Ecuador", -1.5f, -78.5f, "HC HD"},
        {"French Guiana", 4, -53, "FY"},
        // --- Africa ---
        {"Egypt", 27, 30, "SU"},
        {"Libya", 27, 17, "5A"},
        {"Tunisia", 34, 9, "3V TS"},
        {"Algeria", 28, 3, "7X 7T 7U 7V 7W 7Y"},
        {"Morocco", 32, -6, "CN"},
        {"Cape Verde", 15, -24, "D4"},
        {"Senegal", 14.5f, -14.5f, "6W 6V"},
        {"Ghana", 8, -1, "9G"},
        {"Nigeria", 9, 8, "5N 5O"},
        {"Ethiopia", 9, 39, "ET"},
        {"Kenya", 0, 38, "5Z 5Y"},
        {"Tanzania", -6, 35, "5H 5I"},
        {"Namibia", -22, 17, "V5"},
        {"South Africa", -30, 25, "ZR ZS ZT ZU"},
        {"Madagascar", -19, 47, "5R 5S"},
        {"Reunion", -21.1f, 55.5f, "FR"},
        {"Mayotte", -12.8f, 45.2f, "FH"},
        {"Mauritius", -20.3f, 57.6f, "3B8"},
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
