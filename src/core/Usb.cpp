#include "Usb.h"
#include "Updater.h"
#include "Version.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <Update.h>
#include <sys/time.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include "AprsIs.h"
#include "Ble.h"
#include "Config.h"
#include "Gps.h"
#include "Log.h"
#include "LoraAprs.h"
#include "Mesh.h"
#include "Net.h"
#include "Settings.h"
#include "Station.h"
#include "Storage.h"
#include "System.h"
#include "lib/Adif.h"
#include "lib/Base64.h"
#include "lib/MiniJson.h"

namespace usb {

namespace {

constexpr const char* kVersion = HAMOS_VERSION;
constexpr size_t kMaxLine = 12000;              // longest request (a file piece in base64)
constexpr size_t kMaxRead = 3072;               // bytes of a file per request

Host host = {};
std::string inLine, outBuf, webBuf;
bool mirrorOn = false, webMirrorOn = false, webOverflow = false;
uint32_t lastRequestMs = 0, lastWebMs = 0;
bool everTalked = false;
constexpr size_t kWebBufMax = 12 * 1024;      // the page asks twice a second; more means it fell behind
bool requestFromWeb = false;            // the request being handled came over WiFi
int deferredApply = 0;                  // settings::apply() after the WiFi reply has gone out
uint32_t rebootAtMs = 0;

void flush() {
    if (outBuf.empty()) return;
    const uint8_t* p = (const uint8_t*)outBuf.data();
    size_t left = outBuf.size();
    uint32_t start = millis();
    while (left > 0) {
        size_t n = Serial.write(p, left);
        p += n;
        left -= n;
        if (n == 0) {
            if (millis() - start > 300) {        // nobody reading: stop mirroring, drop the rest
                mirrorOn = false;
                break;
            }
            delay(1);
        }
    }
    outBuf.clear();
}

void emit(const std::string& line) {
    outBuf += line;
    outBuf += '\n';
    if (outBuf.size() > 4096) flush();
}

// A drawing step: to the USB port and/or the web page.
void emitScreen(const std::string& line) {
    if (mirrorOn) emit(line);
    if (webMirrorOn) {
        if (webBuf.size() + line.size() + 1 > kWebBufMax) {
            webOverflow = true;                 // the page fell behind: it gets a full redraw instead
            webBuf.clear();
        } else {
            webBuf += line;
            webBuf += '\n';
        }
    }
}

std::string err(int id, const std::string& message) {
    json::Writer w;
    w.obj().kv("id", id).kv("ok", false).kv("error", message).end();
    return w.out;
}

json::Writer& begin(json::Writer& w, int id) {
    w.obj().kv("id", id).kv("ok", true);
    return w;
}

bool safePath(const std::string& p) { return !p.empty() && p[0] == '/' && p.find("..") == std::string::npos; }

std::string timeText(time_t t) {
    if (!t) return "";
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return buf;
}

// ---- commands ----

std::string status(int id, bool full) {
    json::Writer w;
    begin(w, id);
    w.kv("name", "HamOS").kv("version", kVersion).kv("callsign", config::get("callsign")).kv("grid", station::grid());
    w.kv("battery", (int)M5Cardputer.Power.getBatteryLevel()).kv("heap", (unsigned long)sys::freeHeap()).kv("uptime", (unsigned long)millis());
    w.kv("timeValid", net::timeValid()).kv("utc", net::timeValid() ? (long long)net::utcMillis() : 0LL);
    w.kv("wifi", net::connected()).kv("ip", net::address()).kv("sd", storage::ready());
    w.kv("ssid", net::ssid()).kv("rssi", net::rssi()).kv("host", net::hostname() + ".local");
    w.kv("reset", sys::resetReason()).kv("resetFault", sys::resetWasFault()).kv("minHeap", (unsigned long)sys::minFreeHeap());
    w.key("ble").obj().kv("on", config::get("mesh_ble", "0") == "1").kv("name", ble::meshName()).kv("app", ble::meshConnected()).end();
    w.key("gps").obj().kv("present", gps::present()).kv("fix", gps::hasFix());
    if (gps::hasFix()) w.kv("lat", gps::fix().lat).kv("lon", gps::fix().lon).kv("sats", gps::fix().satellites);
    w.end();
    w.key("mesh").obj().kv("ready", mesh::ready()).kv("status", mesh::status()).kv("unread", mesh::unread()).end();
    w.key("aprs").obj().kv("online", aprsis::online()).kv("status", aprsis::status()).kv("unread", aprsis::unread())
        .kv("lora", loraaprs::ready()).kv("loraStatus", loraaprs::status()).end();
    w.kv("log", (unsigned long)logbook::total());
    App* cur = host.current ? host.current() : nullptr;
    w.kv("app", cur ? cur->name() : "");
    if (full && host.apps) {
        w.key("apps").arr();
        for (App* a : *host.apps) w.val(a->name());
        w.end();
    }
    w.end();
    return w.out;
}

std::string configList(int id) {
    json::Writer w;
    begin(w, id);
    w.key("fields").arr();
    for (const settings::Field& f : settings::fields())
        w.obj().kv("key", f.key).kv("label", f.label).kv("default", f.def).kv("value", config::get(f.key, ""))
            .kv("secret", f.secret).kv("max", (int)f.maxLen).end();
    w.end();
    w.key("other").obj();                       // keys only set by hand in config.txt
    for (const auto& kv : config::all())
        if (!settings::find(kv.first.c_str())) w.kv(kv.first, kv.second);
    w.end().end();
    return w.out;
}

std::string configSet(int id, const json::Object& r) {
    std::string key = r.str("key"), value = r.str("value");
    if (key.empty() || key.find_first_of("=\n\r") != std::string::npos) return err(id, "bad key");
    if (value.find_first_of("\n\r") != std::string::npos) return err(id, "bad value");
    if (value.empty()) config::erase(key);
    else config::set(key, value);
    bool saved = config::save();
    if (const settings::Field* f = settings::find(key.c_str())) {
        // Restarting WiFi now would drop the web page's connection before it gets the reply.
        if (requestFromWeb && (f->apply & settings::WIFI)) deferredApply |= f->apply;
        else settings::apply(f->apply);
    }
    if (host.redraw) host.redraw();
    json::Writer w;
    begin(w, id).kv("saved", saved).end();
    return w.out;
}

std::string key(int id, const json::Object& r) {
    if (!host.key) return err(id, "not ready");
    KeyEvent k;
    std::string name = r.str("key"), ch = r.str("ch");
    k.fn = r.flag("fn");
    if (name == "enter") k.enter = true;
    else if (name == "del") k.del = true;
    else if (name == "tab") k.tab = true;
    else if (name == "esc") k.esc = true;
    else if (name == "up") k.arrowUp = true;
    else if (name == "down") k.arrowDown = true;
    else if (name == "left") k.arrowLeft = true;
    else if (name == "right") k.arrowRight = true;
    else if (ch.size() == 1 && ch[0] >= 32 && ch[0] < 127) k.ch = ch[0];
    else return err(id, "unknown key");
    host.key(k);
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string openApp(int id, const json::Object& r) {
    if (!host.open || !host.apps) return err(id, "not ready");
    std::string name = r.str("name");
    if (name.empty() || name == "home") {
        host.open(nullptr);
    } else {
        App* found = nullptr;
        for (App* a : *host.apps)
            if (name == a->name()) found = a;
        if (!found) return err(id, "no app called " + name);
        host.open(found);
    }
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string timeSet(int id, const json::Object& r) {
    double ms = r.num("ms");
    if (ms < 1.7e12) return err(id, "bad time");
#ifndef HAMOS_SIM
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)(((long long)ms % 1000) * 1000);
    settimeofday(&tv, nullptr);
#endif
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string filesList(int id, const json::Object& r) {
    std::string path = r.str("path", "/");
    if (!safePath(path)) return err(id, "bad path");
    if (!storage::ready()) return err(id, "no SD card");
    json::Writer w;
    begin(w, id).kv("path", path).key("entries").arr();
    for (const storage::Entry& e : storage::listDir(path.c_str()))
        w.obj().kv("name", e.name).kv("dir", e.folder).kv("size", (unsigned long)e.size).end();
    w.end().end();
    return w.out;
}

std::string filesRead(int id, const json::Object& r) {
    std::string path = r.str("path");
    if (!safePath(path)) return err(id, "bad path");
    uint32_t size = storage::fileSize(path.c_str());
    uint32_t offset = (uint32_t)r.num("offset");
    size_t len = (size_t)r.num("len", kMaxRead);
    if (len > kMaxRead) len = kMaxRead;
    if (!storage::exists(path.c_str())) return err(id, "no such file");
    std::string buf(len, '\0');
    size_t got = offset < size ? storage::readAt(path.c_str(), offset, &buf[0], len) : 0;
    json::Writer w;
    begin(w, id).kv("size", (unsigned long)size).kv("offset", (unsigned long)offset)
        .kv("data", base64::encode((const uint8_t*)buf.data(), got)).kv("eof", offset + got >= size).end();
    return w.out;
}

std::string filesWrite(int id, const json::Object& r) {
    std::string path = r.str("path");
    if (!safePath(path)) return err(id, "bad path");
    std::string data = base64::decode(r.str("data"));
    uint32_t offset = (uint32_t)r.num("offset");
    bool ok;
    if (offset == 0) ok = storage::writeFile(path.c_str(), data);
    else if (storage::fileSize(path.c_str()) != offset) return err(id, "offset does not match the file size");
    else ok = storage::appendFile(path.c_str(), data);
    if (!ok) return err(id, "write failed");
    json::Writer w;
    begin(w, id).kv("size", (unsigned long)storage::fileSize(path.c_str())).end();
    return w.out;
}

// ---- firmware from the PC: fw.begin {size}, fw.write {offset, data}, fw.end ----
// The image goes into the other app slot; nothing changes until fw.end has
// checked it. After a restart the boot guard watches the new version.
size_t fwSize = 0, fwDone = 0;
bool fwActive = false;

std::string fwBegin(int id, const json::Object& r) {
    if (fwActive) Update.abort();
    fwSize = (size_t)r.num("size");
    fwDone = 0;
    if (fwSize < 100000) return err(id, "not a firmware image");
    fwActive = Update.begin(fwSize);
    if (!fwActive) return err(id, "not enough room for the update");
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string fwWrite(int id, const json::Object& r) {
    if (!fwActive) return err(id, "fw.begin first");
    if ((size_t)r.num("offset") != fwDone) return err(id, "offset does not match");
    std::string data = base64::decode(r.str("data"));
    if (Update.write((uint8_t*)data.data(), data.size()) != data.size()) {
        Update.abort();
        fwActive = false;
        return err(id, "write failed");
    }
    fwDone += data.size();
    json::Writer w;
    begin(w, id).kv("done", (unsigned long)fwDone).end();
    return w.out;
}

std::string fwEnd(int id) {
    if (!fwActive) return err(id, "fw.begin first");
    fwActive = false;
    if (fwDone != fwSize) {
        Update.abort();
        return err(id, "incomplete image");
    }
    if (!Update.end(true)) return err(id, "the file is not a valid firmware image");
    updater::afterManualUpdate();
    rebootAtMs = millis() + 800;            // after the reply
    if (!rebootAtMs) rebootAtMs = 1;
    json::Writer w;
    begin(w, id).kv("restarting", true).end();
    return w.out;
}

// ---- settings backup: config.export -> text, config.import {text} ----
std::string configExport(int id) {
    std::string text;
    for (const auto& kv : config::all()) text += kv.first + "=" + kv.second + "\n";
    json::Writer w;
    begin(w, id).kv("text", text).end();
    return w.out;
}

std::string configImport(int id, const json::Object& r) {
    std::string text = r.str("text");
    int n = 0;
    size_t a = 0;
    while (a < text.size()) {
        size_t e = text.find('\n', a);
        std::string line = text.substr(a, e == std::string::npos ? std::string::npos : e - a);
        a = e == std::string::npos ? text.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0) continue;
        config::set(line.substr(0, eq), line.substr(eq + 1));
        n++;
    }
    if (!n) return err(id, "no settings in the file");
    bool saved = config::save();
    settings::apply(settings::WIFI | settings::MESH | settings::APRS | settings::RIG | settings::FT8 | settings::LANG);
    if (host.redraw) host.redraw();
    json::Writer w;
    begin(w, id).kv("count", n).kv("saved", saved).end();
    return w.out;
}

std::string filesDelete(int id, const json::Object& r) {
    std::string path = r.str("path");
    if (!safePath(path) || path == "/") return err(id, "bad path");
    bool ok = storage::remove(path.c_str()) || storage::removeDir(path.c_str());
    if (!ok) return err(id, "could not delete (a folder must be empty)");
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string filesMkdir(int id, const json::Object& r) {
    std::string path = r.str("path");
    if (!safePath(path)) return err(id, "bad path");
    if (!storage::makeDir(path.c_str())) return err(id, "could not create");
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string logList(int id) {
    logbook::load();
    const auto& q = logbook::recent();
    json::Writer w;
    begin(w, id).kv("total", (unsigned long)logbook::total()).key("qsos").arr();
    for (size_t i = 0; i < q.size(); i++) {
        const adif::Qso& x = q[i];
        w.obj().kv("index", (unsigned long)i).kv("call", x.call).kv("date", x.date).kv("time", x.time).kv("freq", x.freq)
            .kv("band", adif::band(atof(x.freq.c_str()))).kv("mode", x.mode).kv("rstSent", x.rstSent).kv("rstRcvd", x.rstRcvd)
            .kv("grid", x.grid).kv("comment", x.comment).kv("pota", x.potaRef).kv("stx", x.stx).kv("srx", x.srx).end();
    }
    w.end().end();
    return w.out;
}

std::string logAdd(int id, const json::Object& r) {
    adif::Qso q;
    q.call = r.str("call");
    for (char& c : q.call) c = (char)toupper((unsigned char)c);
    if (q.call.empty()) return err(id, "callsign required");
    q.freq = r.str("freq");
    q.mode = r.str("mode");
    for (char& c : q.mode) c = (char)toupper((unsigned char)c);
    q.rstSent = r.str("rstSent");
    q.rstRcvd = r.str("rstRcvd");
    q.grid = r.str("grid");
    q.comment = r.str("comment");
    q.potaRef = config::get("pota_ref");
    q.sotaRef = config::get("sota_ref");
    q.date = r.str("date");
    q.time = r.str("time");
    if (q.date.empty() && net::timeValid()) {
        time_t now = time(nullptr);
        struct tm t;
        gmtime_r(&now, &t);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y%m%d", &t);
        q.date = buf;
        strftime(buf, sizeof(buf), "%H%M", &t);
        q.time = buf;
    }
    bool ok = logbook::add(q);
    if (host.redraw) host.redraw();
    json::Writer w;
    begin(w, id).kv("saved", ok).kv("total", (unsigned long)logbook::total()).end();
    return w.out;
}

std::string logDelete(int id, const json::Object& r) {
    size_t index = (size_t)r.num("index", -1);
    std::string call = r.str("call");
    const auto& q = logbook::recent();
    if (index >= q.size()) return err(id, "no such contact");
    if (!call.empty() && q[index].call != call) return err(id, "the log has changed, reload it");
    if (!logbook::remove(index)) return err(id, "delete failed");
    if (host.redraw) host.redraw();
    json::Writer w;
    begin(w, id).kv("total", (unsigned long)logbook::total()).end();
    return w.out;
}

std::string meshMessages(int id) {
    json::Writer w;
    begin(w, id).kv("ready", mesh::ready()).kv("status", mesh::status()).key("messages").arr();
    for (const mesh::Message& m : mesh::messages())
        w.obj().kv("from", mesh::nodeName(m.from)).kv("short", mesh::nodeName(m.from, true)).kv("text", m.text)
            .kv("time", timeText(m.utc)).kv("mine", m.mine).kv("direct", m.direct).kv("sent", m.sent).kv("relayed", m.relayed).end();
    w.end().key("nodes").arr();
    for (const mesh::Node& n : mesh::nodes()) {
        char idText[12];
        snprintf(idText, sizeof(idText), "!%08x", (unsigned)n.id);
        w.obj().kv("id", idText).kv("long", mesh::nodeName(n.id)).kv("short", mesh::nodeName(n.id, true)).kv("snr", n.snr)
            .kv("rssi", n.rssi).kv("hops", n.hops).kv("ago", (unsigned long)((millis() - n.lastHeardMs) / 1000));
        if (n.hasPosition) w.kv("lat", n.lat).kv("lon", n.lon);
        w.end();
    }
    w.end().end();
    mesh::markRead();
    return w.out;
}

std::string meshSend(int id, const json::Object& r) {
    std::string error;
    if (!mesh::sendText(r.str("text"), error)) return err(id, error);
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

std::string aprsList(int id) {
    json::Writer w;
    begin(w, id).kv("online", aprsis::online()).kv("status", aprsis::status()).kv("lora", loraaprs::ready()).key("stations").arr();
    static const char* via[3] = {"internet", "audio", "lora"};
    for (const aprsis::Station& s : aprsis::stations())
        w.obj().kv("call", s.call).kv("lat", s.lat).kv("lon", s.lon).kv("symbol", std::string(1, s.symbolTable) + s.symbol)
            .kv("comment", s.comment).kv("via", via[s.source]).kv("ago", (unsigned long)((millis() - s.lastHeardMs) / 1000)).end();
    w.end().key("messages").arr();
    for (const aprsis::Message& m : aprsis::messages())
        w.obj().kv("from", m.from).kv("to", m.to).kv("text", m.text).kv("mine", m.mine).kv("acked", m.acked)
            .kv("via", via[m.source]).kv("path", m.path).kv("ago", (unsigned long)((millis() - m.atMs) / 1000)).end();
    w.end().end();
    aprsis::markRead();
    return w.out;
}

std::string aprsSend(int id, const json::Object& r) {
    std::string error;
    bool ok = r.str("cmd") == "aprs.beacon" ? aprsis::sendBeacon(error) : aprsis::sendMessage(r.str("to"), r.str("text"), error);
    if (!ok) return err(id, error);
    json::Writer w;
    begin(w, id).end();
    return w.out;
}

}  // namespace

std::string handle(const std::string& line, bool fromWeb) {
    json::Object r;
    if (!json::parse(line, r)) return err(0, "not a JSON object");
    int id = (int)r.num("id");
    std::string cmd = r.str("cmd");
    requestFromWeb = fromWeb;
    if (fromWeb) {
        lastWebMs = millis();
    } else {
        lastRequestMs = millis();
        everTalked = true;
    }
    if (cmd == "ping") { json::Writer w; begin(w, id).end(); return w.out; }
    if (cmd == "hello") return status(id, true);
    if (cmd == "status") return status(id, false);
    if (cmd == "screen.mirror") {
        bool on = r.flag("on");
        if (fromWeb) {
            webMirrorOn = on;
            webBuf.clear();
        } else {
            mirrorOn = on;
        }
        if (on && host.redraw) host.redraw();
        json::Writer w;
        begin(w, id).kv("mirror", on).kv("width", 240).kv("height", 135).end();
        return w.out;
    }
    if (cmd == "screen.redraw") { if (host.redraw) host.redraw(); json::Writer w; begin(w, id).end(); return w.out; }
    if (cmd == "key") return key(id, r);
    if (cmd == "app.open") return openApp(id, r);
    if (cmd == "config.list") return configList(id);
    if (cmd == "config.set") return configSet(id, r);
    if (cmd == "time.set") return timeSet(id, r);
    if (cmd == "files.list") return filesList(id, r);
    if (cmd == "files.read") return filesRead(id, r);
    if (cmd == "files.write") return filesWrite(id, r);
    if (cmd == "files.delete") return filesDelete(id, r);
    if (cmd == "files.mkdir") return filesMkdir(id, r);
    if (cmd == "log.list") return logList(id);
    if (cmd == "log.add") return logAdd(id, r);
    if (cmd == "log.delete") return logDelete(id, r);
    if (cmd == "mesh.list") return meshMessages(id);
    if (cmd == "mesh.send") return meshSend(id, r);
    if (cmd == "aprs.list") return aprsList(id);
    if (cmd == "aprs.send" || cmd == "aprs.beacon") return aprsSend(id, r);
    if (cmd == "fw.begin") return fwBegin(id, r);
    if (cmd == "fw.write") return fwWrite(id, r);
    if (cmd == "fw.end") return fwEnd(id);
    if (cmd == "config.export") return configExport(id);
    if (cmd == "config.import") return configImport(id, r);
    if (cmd == "reboot") {
        if (fromWeb) {                          // after the reply has been sent, from tick()
            rebootAtMs = millis() + 500;
            if (!rebootAtMs) rebootAtMs = 1;
            return json::Writer().obj().kv("id", id).kv("ok", true).end().out;
        }
        emit(json::Writer().obj().kv("id", id).kv("ok", true).end().out);
        flush();
        delay(200);
#ifndef HAMOS_SIM
        ESP.restart();
#endif
        return "";
    }
    return err(id, "unknown command " + cmd);
}

void begin(const Host& h) {
    host = h;
#if ARDUINO_USB_MODE && !defined(HAMOS_SIM)
    Serial.setRxBufferSize(kMaxLine + 512);     // a whole request fits while the loop is busy drawing
    Serial.setTxBufferSize(4096);
    Serial.setTxTimeoutMs(20);
#endif
    Serial.begin(115200);
}

void tick() {
    for (int budget = 16384; budget > 0 && Serial.available(); budget--) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\n') {
            if (!inLine.empty() && inLine.back() == '\r') inLine.pop_back();
            if (!inLine.empty() && inLine[0] == '{') {
                std::string reply = handle(inLine);
                if (!reply.empty()) emit(reply);
            }
            inLine.clear();
        } else if (inLine.size() < kMaxLine) {
            inLine += (char)c;
        }
    }
    // The PC sends something at least every few seconds while it watches the screen.
    if (mirrorOn && millis() - lastRequestMs > 10000) mirrorOn = false;
    if (deferredApply && millis() - lastWebMs > 300) {
        int what = deferredApply;
        deferredApply = 0;
        settings::apply(what);
    }
    if (rebootAtMs && (int32_t)(millis() - rebootAtMs) >= 0) {
        rebootAtMs = 0;
#ifndef HAMOS_SIM
        ESP.restart();
#endif
    }
    if (webMirrorOn && millis() - lastWebMs > 10000) {
        webMirrorOn = false;
        webBuf.clear();
    }
    flush();
}

std::string webScreen() {
    lastWebMs = millis();
    if (webOverflow) {
        webOverflow = false;
        webBuf.clear();
        if (host.redraw) host.redraw();
    }
    std::string out;
    out.swap(webBuf);
    return out;
}

bool connected() { return everTalked && millis() - lastRequestMs < 5000; }
bool mirroring() { return mirrorOn || webMirrorOn; }

static void op(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void op(const char* fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emitScreen(buf);
}

void mirrorFill(int x, int y, int w, int h, uint16_t c) { op("~R %d %d %d %d %u", x, y, w, h, c); }
void mirrorLine(int x0, int y0, int x1, int y1, uint16_t c) { op("~L %d %d %d %d %u", x0, y0, x1, y1, c); }
void mirrorCircle(int x, int y, int r, uint16_t c, bool filled) { op("~C %d %d %d %u %d", x, y, r, c, filled ? 1 : 0); }
void mirrorTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    op("~G %d %d %d %d %d %d %u", x0, y0, x1, y1, x2, y2, c);
}
void mirrorFrame(int x, int y, int w, int h, uint16_t c) { op("~F %d %d %d %d %u", x, y, w, h, c); }

void mirrorText(int x, int y, float size, uint16_t fg, int bg, const std::string& text) {
    char head[64];
    snprintf(head, sizeof(head), "~T %d %d %g %u %d ", x, y, size, fg, bg);
    emitScreen(head + text);
}

void mirrorImage(int x, int y, int w, int h, const uint16_t* data) {
    char head[48];
    snprintf(head, sizeof(head), "~I %d %d %d %d ", x, y, w, h);
    emitScreen(head + base64::encode((const uint8_t*)data, (size_t)w * h * 2));
}

}  // namespace usb
