#include "Log.h"
#include <cstdlib>
#include <set>
#include "Storage.h"

namespace logbook {

namespace {

std::vector<adif::Qso> recentQsos;
std::set<const dxcc::Entity*> entities;
size_t totalCount = 0;
bool loaded = false;

// Position just past the next "<EOR>" (any letter case) in s, npos if none.
size_t recordEnd(const std::string& s) {
    for (size_t i = 0; i + 5 <= s.size(); i++)
        if (s[i] == '<' && (s[i + 1] | 0x20) == 'e' && (s[i + 2] | 0x20) == 'o' && (s[i + 3] | 0x20) == 'r' && s[i + 4] == '>')
            return i + 5;
    return std::string::npos;
}

// Cuts a stream of file chunks into pieces that each end with <EOR>.
struct Splitter {
    std::string buf;
    void (*onPiece)(const std::string& piece, void* user);
    void* user;
    static void feed(const char* data, size_t len, void* self) {
        Splitter& s = *(Splitter*)self;
        s.buf.append(data, len);
        size_t end;
        while ((end = recordEnd(s.buf)) != std::string::npos) {
            s.onPiece(s.buf.substr(0, end), s.user);
            s.buf.erase(0, end);
        }
        if (s.buf.size() > 8192) s.buf.erase(0, s.buf.size() - 16);   // damaged file: no record is this long
    }
};

void remember(const adif::Qso& q) {
    totalCount++;
    recentQsos.push_back(q);
    if (recentQsos.size() > MAX_RECENT) recentQsos.erase(recentQsos.begin());
    if (const dxcc::Entity* e = dxcc::lookup(q.call)) entities.insert(e);
}

void reload() {
    recentQsos.clear();
    entities.clear();
    totalCount = 0;
    Splitter s;
    s.onPiece = [](const std::string& piece, void*) {
        for (const adif::Qso& q : adif::parse(piece)) remember(q);
    };
    s.user = nullptr;
    storage::readChunks(kPath, Splitter::feed, &s);
    loaded = true;
}

struct Rewrite {
    size_t skip;            // index (among valid contacts) of the one to drop
    size_t seen = 0, dropped = 0;
    std::string out;
    bool ok = true;
    void flush() {
        if (!out.empty() && !storage::appendFile("/HamOS/log.tmp", out)) ok = false;
        out.clear();
    }
};

}  // namespace

void load() {
    if (!loaded) reload();
}

const std::vector<adif::Qso>& recent() { return recentQsos; }

void forEach(void (*fn)(const adif::Qso& q, void* user), void* user) {
    struct Ctx {
        void (*fn)(const adif::Qso&, void*);
        void* user;
    } ctx{fn, user};
    Splitter s;
    s.user = &ctx;
    s.onPiece = [](const std::string& piece, void* u) {
        Ctx* c = (Ctx*)u;
        for (const adif::Qso& q : adif::parse(piece)) c->fn(q, c->user);
    };
    storage::readChunks(kPath, Splitter::feed, &s);
}
size_t total() { return totalCount; }
bool workedEntity(const dxcc::Entity* e) { return e && entities.count(e) > 0; }

static void (*addHook)(const adif::Qso&) = nullptr;
void onAdd(void (*fn)(const adif::Qso& q)) { addHook = fn; }

bool add(const adif::Qso& q) {
    load();
    if (addHook) addHook(q);
    std::string out = storage::exists(kPath) ? "" : adif::header();
    bool ok = storage::appendFile(kPath, out + adif::toRecord(q));
    remember(q);
    return ok;
}

bool remove(size_t indexInRecent) {
    if (indexInRecent >= recentQsos.size() || !storage::ready()) return false;
    Rewrite rw;
    rw.skip = totalCount - recentQsos.size() + indexInRecent;
    storage::remove("/HamOS/log.tmp");

    Splitter s;
    s.user = &rw;
    s.onPiece = [](const std::string& piece, void* user) {
        Rewrite& r = *(Rewrite*)user;
        bool valid = !adif::parse(piece).empty();
        if (valid && r.seen++ == r.skip) {
            r.dropped++;
            // The first record shares its piece with the file header: keep the header.
            for (size_t i = 0; i + 5 <= piece.size(); i++)
                if (piece[i] == '<' && (piece[i + 1] | 0x20) == 'e' && (piece[i + 2] | 0x20) == 'o' &&
                    (piece[i + 3] | 0x20) == 'h' && piece[i + 4] == '>') {
                    r.out += piece.substr(0, i + 5);
                    break;
                }
        } else {
            r.out += piece;
        }
        if (r.out.size() > 2048) r.flush();
    };
    if (!storage::readChunks(kPath, Splitter::feed, &s)) return false;
    rw.out += s.buf;        // whatever follows the last record
    rw.flush();

    // Only replace the log if exactly the intended contact was removed and every write succeeded.
    if (!rw.ok || rw.dropped != 1 || !storage::exists("/HamOS/log.tmp")) {
        storage::remove("/HamOS/log.tmp");
        return false;
    }
    storage::remove("/HamOS/log.bak");
    if (!storage::rename(kPath, "/HamOS/log.bak")) return false;
    if (!storage::rename("/HamOS/log.tmp", kPath)) {
        storage::rename("/HamOS/log.bak", kPath);
        return false;
    }
    reload();
    return true;
}

int worked(const std::string& call, const std::string& freq, const std::string& mode, int& dupes) {
    int n = 0;
    dupes = 0;
    std::string band = adif::band(atof(freq.c_str()));
    for (const adif::Qso& q : recentQsos) {
        if (q.call != call) continue;
        n++;
        if (q.mode == mode && adif::band(atof(q.freq.c_str())) == band) dupes++;
    }
    return n;
}

}  // namespace logbook
