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
    const char* continent;  // EU AS AF NA SA OC (Turkey counts as AS, as in cty.dat)
};

inline const Entity* table(size_t& count) {
    static const Entity e[] = {
        // --- Europe ---
        {"Turkey", 39, 35, "TA TB TC YM", "tr", "AS"},
        {"Germany", 51, 10, "DA DB DC DD DE DF DG DH DI DJ DK DL DM DN DO DP DQ DR", "de", "EU"},
        {"England", 52.5f, -1.5f, "G M 2E", "gb-eng", "EU"},
        {"Scotland", 57, -4, "GM GS MM MS 2M", "gb-sct", "EU"},
        {"Wales", 52.3f, -3.7f, "GW GC MW MC 2W", "gb-wls", "EU"},
        {"N. Ireland", 54.6f, -6.7f, "GI GN MI MN 2I", "gb-nir", "EU"},
        {"Isle of Man", 54.2f, -4.5f, "GD GT MD MT 2D", "im", "EU"},
        {"Jersey", 49.2f, -2.1f, "GJ GH MJ MH 2J", "je", "EU"},
        {"Guernsey", 49.5f, -2.6f, "GU GP MU MP 2U", "gg", "EU"},
        {"Ireland", 53.2f, -8, "EI EJ", "ie", "EU"},
        {"France", 46.5f, 2.5f, "F TM", "fr", "EU"},
        {"Corsica", 42, 9, "TK", "fr", "EU"},
        {"Italy", 42.5f, 12.5f, "I", "it", "EU"},
        {"Sardinia", 40, 9, "IS0 IM0", "it", "EU"},
        {"Spain", 40, -3.7f, "EA EB EC ED EE EF EG EH AM AN AO", "es", "EU"},
        {"Balearic Is.", 39.6f, 3, "EA6 EB6 EC6 ED6 EE6 EF6", "es", "EU"},
        {"Canary Is.", 28.3f, -15.8f, "EA8 EB8 EC8 ED8 EE8 EF8", "es", "EU"},
        {"Ceuta & Melilla", 35.6f, -4, "EA9 EB9 EC9 ED9 EE9 EF9", "es", "EU"},
        {"Portugal", 39.5f, -8, "CT CQ CR CS", "pt", "EU"},
        {"Madeira", 32.7f, -17, "CT3 CT9 CQ3 CQ9", "pt", "EU"},
        {"Azores", 38.5f, -28, "CU", "pt", "EU"},
        {"Netherlands", 52.2f, 5.5f, "PA PB PC PD PE PF PG PH PI", "nl", "EU"},
        {"Belgium", 50.6f, 4.6f, "ON OO OP OQ OR OS OT", "be", "EU"},
        {"Luxembourg", 49.7f, 6.1f, "LX", "lu", "EU"},
        {"Switzerland", 46.8f, 8.2f, "HB HE", "ch", "EU"},
        {"Liechtenstein", 47.1f, 9.5f, "HB0", "li", "EU"},
        {"Austria", 47.5f, 14, "OE", "at", "EU"},
        {"Czech Rep.", 49.8f, 15.5f, "OK OL", "cz", "EU"},
        {"Slovakia", 48.7f, 19.5f, "OM", "sk", "EU"},
        {"Poland", 52, 19, "SN SO SP SQ SR 3Z HF", "pl", "EU"},
        {"Hungary", 47, 19.5f, "HA HG", "hu", "EU"},
        {"Romania", 46, 25, "YO YP YQ YR", "ro", "EU"},
        {"Bulgaria", 42.7f, 25.3f, "LZ", "bg", "EU"},
        {"Greece", 39, 22, "SV SW SX SY SZ J4", "gr", "EU"},
        {"Dodecanese", 36.4f, 28, "SV5 SW5 SX5 J45", "gr", "EU"},
        {"Crete", 35.2f, 25, "SV9 SW9 SX9 J49", "gr", "EU"},
        {"Cyprus", 35, 33, "5B C4 H2 P3", "cy", "EU"},
        {"Serbia", 44, 21, "YT YU", "rs", "EU"},
        {"Croatia", 45, 16, "9A", "hr", "EU"},
        {"Slovenia", 46, 15, "S5", "si", "EU"},
        {"Bosnia-Herz.", 44, 18, "E7", "ba", "EU"},
        {"Montenegro", 42.7f, 19.3f, "4O", "me", "EU"},
        {"N. Macedonia", 41.6f, 21.7f, "Z3", "mk", "EU"},
        {"Albania", 41, 20, "ZA", "al", "EU"},
        {"Kosovo", 42.6f, 21, "Z6", "xk", "EU"},
        {"Ukraine", 49, 32, "UR US UT UU UV UW UX UY UZ EM EN EO", "ua", "EU"},
        {"Belarus", 53.5f, 28, "EU EV EW", "by", "EU"},
        {"Moldova", 47.2f, 28.5f, "ER", "md", "EU"},
        {"Lithuania", 55.3f, 24, "LY", "lt", "EU"},
        {"Latvia", 57, 25, "YL", "lv", "EU"},
        {"Estonia", 58.7f, 25.5f, "ES", "ee", "EU"},
        {"European Russia", 55.7f, 37.6f, "R U", "ru", "EU"},
        {"Kaliningrad", 54.7f, 20.5f, "UA2 RA2 R2F RK2F", "ru", "EU"},
        {"Finland", 62, 26, "OF OG OH OI OJ", "fi", "EU"},
        {"Aland Is.", 60.2f, 20, "OH0 OF0 OG0", "ax", "EU"},
        {"Sweden", 60, 15, "SA SB SC SD SE SF SG SH SI SJ SK SL SM 7S 8S", "se", "EU"},
        {"Norway", 61, 9, "LA LB LC LD LE LF LG LH LI LJ LK LL LM LN", "no", "EU"},
        {"Svalbard", 78, 16, "JW", "sj", "EU"},
        {"Denmark", 56, 10, "OU OV OW OZ 5P 5Q", "dk", "EU"},
        {"Faroe Is.", 62, -7, "OY", "fo", "EU"},
        {"Greenland", 70, -42, "OX", "gl", "EU"},
        {"Iceland", 65, -18, "TF", "is", "EU"},
        {"Malta", 35.9f, 14.4f, "9H", "mt", "EU"},
        {"San Marino", 43.9f, 12.5f, "T7", "sm", "EU"},
        {"Vatican", 41.9f, 12.45f, "HV", "va", "EU"},
        {"Monaco", 43.7f, 7.4f, "3A", "mc", "EU"},
        {"Andorra", 42.5f, 1.5f, "C3", "ad", "EU"},
        {"Gibraltar", 36.1f, -5.3f, "ZB", "gi", "EU"},
        // --- Asia ---
        {"Asiatic Russia", 60, 95, "UA8 UA9 UA0 RA8 RA9 RA0 R8 R9 R0", "ru", "AS"},
        {"Kazakhstan", 48, 68, "UN UO UP UQ", "kz", "AS"},
        {"Uzbekistan", 41, 64, "UJ UK UL UM", "uz", "AS"},
        {"Kyrgyzstan", 41.5f, 75, "EX", "kg", "AS"},
        {"Tajikistan", 38.8f, 71, "EY", "tj", "AS"},
        {"Turkmenistan", 39, 60, "EZ", "tm", "AS"},
        {"Georgia", 42, 43.5f, "4L", "ge", "AS"},
        {"Armenia", 40.3f, 45, "EK", "am", "AS"},
        {"Azerbaijan", 40.4f, 48, "4J 4K", "az", "AS"},
        {"Israel", 31.5f, 35, "4X 4Z", "il", "AS"},
        {"Lebanon", 33.9f, 35.8f, "OD", "lb", "AS"},
        {"Jordan", 31, 36.5f, "JY", "jo", "AS"},
        {"Syria", 35, 38.5f, "YK", "sy", "AS"},
        {"Iraq", 33, 44, "YI", "iq", "AS"},
        {"Iran", 32, 53, "EP EQ", "ir", "AS"},
        {"Saudi Arabia", 24, 45, "HZ 7Z 8Z", "sa", "AS"},
        {"UAE", 24, 54, "A6", "ae", "AS"},
        {"Qatar", 25.3f, 51.2f, "A7", "qa", "AS"},
        {"Bahrain", 26, 50.5f, "A9", "bh", "AS"},
        {"Kuwait", 29.3f, 47.7f, "9K", "kw", "AS"},
        {"Oman", 21, 57, "A4", "om", "AS"},
        {"India", 22, 79, "VU AT AU AV AW", "in", "AS"},
        {"Pakistan", 30, 70, "AP AQ AR AS", "pk", "AS"},
        {"Sri Lanka", 7.5f, 80.7f, "4S", "lk", "AS"},
        {"Nepal", 28, 84, "9N", "np", "AS"},
        {"Bangladesh", 24, 90, "S2", "bd", "AS"},
        {"China", 35, 105, "B", "cn", "AS"},
        {"Taiwan", 23.7f, 121, "BM BN BO BP BQ BU BV BW BX", "tw", "AS"},
        {"Hong Kong", 22.3f, 114.2f, "VR", "hk", "AS"},
        {"Japan", 36, 138, "JA JE JF JG JH JI JJ JK JL JM JN JO JP JQ JR JS 7J 7K 7L 7M 7N 8J 8N", "jp", "AS"},
        {"South Korea", 36.5f, 127.8f, "HL DS DT 6K 6L 6M 6N D7 D8 D9", "kr", "AS"},
        {"Thailand", 15, 101, "HS E2", "th", "AS"},
        {"Vietnam", 16, 107, "XV 3W", "vn", "AS"},
        {"West Malaysia", 4, 102, "9M2 9M4 9W2 9W4", "my", "AS"},
        {"East Malaysia", 4, 115, "9M6 9M8 9W6 9W8", "my", "AS"},
        {"Singapore", 1.35f, 103.8f, "9V", "sg", "AS"},
        {"Indonesia", -2, 118, "YB YC YD YE YF YG YH", "id", "AS"},
        {"Philippines", 13, 122, "DU DV DW DX DY DZ 4D 4E 4F 4G 4H 4I", "ph", "AS"},
        // --- Oceania ---
        {"Australia", -25, 134, "VK AX", "au", "OC"},
        {"New Zealand", -41, 174, "ZL ZM", "nz", "OC"},
        {"Hawaii", 20.8f, -157, "KH6 KH7 AH6 AH7 NH6 NH7 WH6 WH7", "us", "OC"},
        {"Guam", 13.5f, 144.8f, "KH2 AH2 NH2 WH2", "gu", "OC"},
        {"New Caledonia", -21.5f, 165.5f, "FK", "nc", "OC"},
        {"Fr. Polynesia", -17.6f, -149.5f, "FO", "pf", "OC"},
        // --- North America ---
        {"USA", 39, -98, "K W N AA AB AC AD AE AF AG AH AI AJ AK AL", "us", "NA"},
        {"Alaska", 64, -150, "KL AL NL WL", "us", "NA"},
        {"Puerto Rico", 18.2f, -66.5f, "KP3 KP4 NP3 NP4 WP3 WP4", "pr", "NA"},
        {"US Virgin Is.", 18, -64.8f, "KP2 NP2 WP2", "vi", "NA"},
        {"Canada", 50, -95, "VA VB VC VD VE VF VG VO VY CF CG CH CI CJ CK CY CZ", "ca", "NA"},
        {"Mexico", 23, -102, "XA XB XC XD XE XF XG XH XI 4A 4B 4C", "mx", "NA"},
        {"Cuba", 22, -79, "CO CM T4", "cu", "NA"},
        {"Dominican Rep.", 19, -70, "HI", "do", "NA"},
        {"Jamaica", 18.1f, -77.3f, "6Y", "jm", "NA"},
        {"Bahamas", 24.5f, -77, "C6", "bs", "NA"},
        {"Guatemala", 15.5f, -90.3f, "TG TD", "gt", "NA"},
        {"Costa Rica", 10, -84, "TI TE", "cr", "NA"},
        {"Panama", 9, -80, "HP HO H3 H8 H9", "pa", "NA"},
        {"Guadeloupe", 16.2f, -61.6f, "FG", "gp", "NA"},
        {"Martinique", 14.6f, -61, "FM", "mq", "NA"},
        {"St. Pierre & Miq.", 46.8f, -56.3f, "FP", "pm", "NA"},
        // --- South America ---
        {"Brazil", -10, -52, "PP PQ PR PS PT PU PV PW PX PY ZV ZW ZX ZY ZZ", "br", "SA"},
        {"Argentina", -35, -64, "LO LP LQ LR LS LT LU LV LW AY AZ", "ar", "SA"},
        {"Chile", -33, -71, "CA CB CC CD CE XQ XR 3G", "cl", "SA"},
        {"Uruguay", -33, -56, "CV CW CX", "uy", "SA"},
        {"Paraguay", -23, -58, "ZP", "py", "SA"},
        {"Peru", -10, -76, "OA OB OC", "pe", "SA"},
        {"Bolivia", -17, -65, "CP", "bo", "SA"},
        {"Colombia", 4, -73, "HJ HK 5J 5K", "co", "SA"},
        {"Venezuela", 8, -66, "YV YW YX YY 4M", "ve", "SA"},
        {"Ecuador", -1.5f, -78.5f, "HC HD", "ec", "SA"},
        {"French Guiana", 4, -53, "FY", "gf", "SA"},
        // --- Africa ---
        {"Egypt", 27, 30, "SU", "eg", "AF"},
        {"Libya", 27, 17, "5A", "ly", "AF"},
        {"Tunisia", 34, 9, "3V TS", "tn", "AF"},
        {"Algeria", 28, 3, "7X 7T 7U 7V 7W 7Y", "dz", "AF"},
        {"Morocco", 32, -6, "CN", "ma", "AF"},
        {"Cape Verde", 15, -24, "D4", "cv", "AF"},
        {"Senegal", 14.5f, -14.5f, "6W 6V", "sn", "AF"},
        {"Ghana", 8, -1, "9G", "gh", "AF"},
        {"Nigeria", 9, 8, "5N 5O", "ng", "AF"},
        {"Ethiopia", 9, 39, "ET", "et", "AF"},
        {"Kenya", 0, 38, "5Z 5Y", "ke", "AF"},
        {"Tanzania", -6, 35, "5H 5I", "tz", "AF"},
        {"Namibia", -22, 17, "V5", "na", "AF"},
        {"South Africa", -30, 25, "ZR ZS ZT ZU", "za", "AF"},
        {"Madagascar", -19, 47, "5R 5S", "mg", "AF"},
        {"Reunion", -21.1f, 55.5f, "FR", "re", "AF"},
        {"Mayotte", -12.8f, 45.2f, "FH", "yt", "AF"},
        {"Mauritius", -20.3f, 57.6f, "3B8", "mu", "AF"},
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
