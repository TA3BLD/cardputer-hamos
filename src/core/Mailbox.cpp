#include "Mailbox.h"
#include <Arduino.h>
#include <ctime>
#include "Config.h"
#include "Mesh.h"
#include "Net.h"
#include "Storage.h"

namespace mailbox {

namespace {

constexpr const char* kPath = "/HamOS/mailbox.txt";
constexpr uint32_t kAwayMs = 30 * 60 * 1000;
std::vector<Mail> mails;
uint32_t seen = 0, sentCount = 0;
bool loaded = false;

void save() {
    std::string out;
    for (const Mail& m : mails)
        out += std::to_string(m.to) + "\t" + std::to_string(m.from) + "\t" + std::to_string(m.when) + "\t" + m.text + "\n";
    storage::writeFile(kPath, out);
}

std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

const mesh::Node* node(uint32_t id) {
    for (const mesh::Node& n : mesh::nodes())
        if (n.id == id) return &n;
    return nullptr;
}

}  // namespace

uint32_t resolve(const std::string& nameIn) {
    std::string name = upper(nameIn);
    if (name.size() == 9 && name[0] == '!') return (uint32_t)strtoul(name.c_str() + 1, nullptr, 16);
    for (const mesh::Node& n : mesh::nodes())
        if (upper(n.shortName) == name || upper(n.longName) == name) return n.id;
    return 0;
}

void begin() {
    mails.clear();
    std::string text;
    if (storage::readFile(kPath, text)) {
        size_t a = 0;
        while (a < text.size()) {
            size_t e = text.find('\n', a);
            std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
            a = e == std::string::npos ? text.size() : e + 1;
            size_t t1 = line.find('\t'), t2 = line.find('\t', t1 + 1), t3 = line.find('\t', t2 + 1);
            if (t3 == std::string::npos) continue;
            Mail m;
            m.to = (uint32_t)strtoul(line.substr(0, t1).c_str(), nullptr, 10);
            m.from = (uint32_t)strtoul(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10);
            m.when = atoll(line.substr(t2 + 1, t3 - t2 - 1).c_str());
            m.storedMs = millis();
            m.text = line.substr(t3 + 1);
            mails.push_back(m);
        }
    }
    seen = mesh::messagesAdded();
    loaded = true;
}

void tick() {
    if (!loaded) begin();
    if (config::get("mesh_mailbox", "0") != "1" || !mesh::ready()) {
        seen = mesh::messagesAdded();
        return;
    }
    uint32_t now = millis();
    // New channel messages for someone who is away: keep them.
    const std::vector<mesh::Message>& msgs = mesh::messages();
    uint32_t added = mesh::messagesAdded();
    size_t fresh = added - seen < msgs.size() ? added - seen : msgs.size();
    bool changed = false;
    for (size_t i = msgs.size() - fresh; i < msgs.size(); i++) {
        const mesh::Message& m = msgs[i];
        if (m.direct || m.text.size() < 3 || m.text[0] != '@') continue;
        size_t sp = m.text.find(' ');
        if (sp == std::string::npos) continue;
        uint32_t to = resolve(m.text.substr(1, sp - 1));
        if (!to || to == mesh::myId()) continue;
        const mesh::Node* n = node(to);
        if (n && n->lastHeardMs && now - n->lastHeardMs < kAwayMs) continue;     // around: it heard it itself
        Mail mail;
        mail.to = to;
        mail.from = m.mine ? mesh::myId() : m.from;
        mail.when = net::timeValid() ? net::utcMillis() / 1000 : 0;
        mail.storedMs = now;
        mail.text = m.text.substr(sp + 1);
        mails.push_back(mail);
        changed = true;
    }
    seen = added;
    // Someone with mail is back: deliver.
    for (size_t i = 0; i < mails.size();) {
        Mail& m = mails[i];
        const mesh::Node* n = node(m.to);
        bool back = n && n->lastHeardMs && (int32_t)(n->lastHeardMs - m.storedMs) > 0 && now - n->lastHeardMs < 60000;
        bool old = m.when && net::timeValid() && net::utcMillis() / 1000 - m.when > 48 * 3600;
        if (back) {
            char stamp[8] = "";
            if (m.when) {
                time_t t = (time_t)m.when;
                struct tm g;
                gmtime_r(&t, &g);
                strftime(stamp, sizeof(stamp), " %H:%M", &g);
            }
            std::string text = "[mail from " + mesh::nodeName(m.from, true) + stamp + "] " + m.text;
            std::string err;
            if (mesh::sendTextTo(text.substr(0, 200), m.to, err)) {
                sentCount++;
                mails.erase(mails.begin() + (long)i);
                changed = true;
                continue;
            }
        } else if (old) {
            mails.erase(mails.begin() + (long)i);
            changed = true;
            continue;
        }
        i++;
    }
    if (changed) save();
}

const std::vector<Mail>& waiting() { return mails; }
uint32_t delivered() { return sentCount; }
void remove(size_t i) {
    if (i < mails.size()) {
        mails.erase(mails.begin() + (long)i);
        save();
    }
}

}  // namespace mailbox
