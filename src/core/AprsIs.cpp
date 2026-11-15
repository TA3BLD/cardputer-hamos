#include "AprsIs.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include "Config.h"
#include "Lang.h"
#include "LoraAprs.h"
#include "Net.h"
#include "Station.h"
#include "lib/Aprs.h"

namespace aprsis {

namespace {

constexpr size_t MAX_STATIONS = 40, MAX_MESSAGES = 30;
uint32_t addedCount = 0;
constexpr uint32_t RETRY_MS = 120000, BEACON_MS = 20UL * 60 * 1000;

WiFiClient client;
bool isEnabled = false, loggedIn = false;
std::string statusText = "off", line;
std::vector<Station> stationList;
std::vector<Message> messageList;
int unreadCount = 0, nextId = 1;
uint32_t lastAttemptMs = 0, lastBeaconMs = 0, lastDataMs = 0;
bool attempted = false, beaconSent = false;

std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

bool isForMe(const std::string& to) {
    std::string me = myCall();
    return !me.empty() && (upper(to) == me || upper(to) == me.substr(0, me.find('-')));
}

void sendLine(const std::string& text) {
    client.print((text + "\r\n").c_str());
}

void connect() {
    attempted = true;
    lastAttemptMs = millis();
    std::string host = config::get("aprs_host", "rotate.aprs2.net");
    int port = atoi(config::get("aprs_port", "14580").c_str());
    if (!client.connect(host.c_str(), (uint16_t)port, 3000)) {
        statusText = T("Cannot reach ") + host;
        return;
    }
    std::string login = "user " + myCall() + " pass " + std::to_string(aprs::passcode(myCall())) + " vers HamOS 0.3";
    double lat, lon;
    if (station::position(lat, lon)) {
        char filter[64];
        snprintf(filter, sizeof(filter), " filter r/%.2f/%.2f/%d", lat, lon, atoi(config::get("aprs_range", "50").c_str()));
        login += filter;
    }
    sendLine(login);
    loggedIn = true;
    lastDataMs = millis();
    statusText = T("Connected to ") + host;
    line.clear();
}

}  // namespace

std::string myCall() {
    std::string call = upper(config::get("callsign"));
    if (call.empty()) return "";
    std::string ssid = config::get("aprs_ssid", "7");
    return ssid.empty() || ssid == "0" ? call : call + "-" + ssid;
}

void begin() {
    client.stop();
    loggedIn = attempted = beaconSent = false;
    isEnabled = config::get("aprs_on", "0") == "1";
    statusText = !isEnabled ? (loraaprs::ready() ? T("LoRa-APRS only (APRS-IS off)") : T("APRS-IS is switched off in Settings"))
                 : myCall().empty() ? "Set your callsign in Settings first" : "Waiting for WiFi";
}

// Sends one packet (text after the "SRC>DST,PATH:" header is `info`) the best way available.
static bool sendPacket(const std::string& info) {
    if (loraaprs::ready()) return loraaprs::send(aprs::header(myCall(), "WIDE1-1") + info);
    if (loggedIn) {
        sendLine(aprs::header(myCall()) + info);
        return true;
    }
    return false;
}

bool canSend() { return loggedIn || loraaprs::ready(); }
uint32_t messagesAdded() { return addedCount; }

bool sendRaw(const std::string& tnc2) {
    if (!loggedIn) return false;
    sendLine(tnc2);
    return true;
}

void gate(const std::string& tnc2) {
    if (!loggedIn || config::get("lora_igate", "0") != "1") return;
    size_t colon = tnc2.find(':');
    if (colon == std::string::npos) return;
    std::string head = tnc2.substr(0, colon);
    // Never gate traffic that came from the internet, or generic queries.
    if (head.find("TCPIP") != std::string::npos || head.find("NOGATE") != std::string::npos ||
        head.find("RFONLY") != std::string::npos || tnc2.compare(colon + 1, 1, "?") == 0)
        return;
    sendLine(head + ",qAR," + myCall() + tnc2.substr(colon));
}

void ingest(const std::string& tnc2, Source source) {
    aprs::Packet p;
    if (!aprs::parseTnc2(tnc2, p)) return;
    aprs::Message m = aprs::parseMessage(p.info);
    if (m.valid) {
        if (!isForMe(m.to)) return;
        if (m.isAck || m.isRej) {
            for (Message& sent : messageList)
                if (sent.mine && sent.id == m.id && upper(sent.to) == upper(p.src)) sent.acked = m.isAck;
            return;
        }
        if (!m.id.empty()) {                     // acknowledge the way it came
            if (source == LORA && loraaprs::ready()) loraaprs::send(aprs::header(myCall(), "WIDE1-1") + aprs::formatAck(p.src, m.id));
            else if (loggedIn) sendLine(aprs::header(myCall()) + aprs::formatAck(p.src, m.id));
        }
        for (const Message& old : messageList)                       // senders repeat until acknowledged
            if (!old.mine && old.from == p.src && old.id == m.id && old.text == m.text && !m.id.empty()) return;
        addedCount++;
        if (messageList.size() >= MAX_MESSAGES) messageList.erase(messageList.begin());
        Message in;
        in.from = p.src;
        in.to = m.to;
        in.text = m.text;
        in.id = m.id;
        messageList.push_back(in);
        unreadCount++;
        if (config::get("mesh_beep", "1") == "1") M5Cardputer.Speaker.tone(1500, 60);
        return;
    }
    aprs::Position pos = aprs::parsePosition(p.info);
    if (!pos.valid) pos = aprs::parseMicE(p.dst, p.info);
    if (!pos.valid || upper(p.src) == myCall()) return;
    Station* s = nullptr;
    for (Station& known : stationList)
        if (known.call == p.src) s = &known;
    if (!s) {
        if (stationList.size() >= MAX_STATIONS) {                    // forget the one heard longest ago
            size_t oldest = 0;
            for (size_t i = 1; i < stationList.size(); i++)
                if (millis() - stationList[i].lastHeardMs > millis() - stationList[oldest].lastHeardMs) oldest = i;
            stationList.erase(stationList.begin() + (long)oldest);
        }
        stationList.push_back(Station());
        s = &stationList.back();
        s->call = p.src;
    }
    s->lat = pos.lat;
    s->lon = pos.lon;
    s->symbolTable = pos.symbolTable;
    s->symbol = pos.symbol;
    s->comment = pos.comment;
    s->lastHeardMs = millis();
    s->source = source;
}

void tick() {
    if (!isEnabled || myCall().empty()) return;
    uint32_t now = millis();
    if (!net::connected()) {
        if (loggedIn) { client.stop(); loggedIn = false; statusText = T("Waiting for WiFi"); }
        return;
    }
    if (!loggedIn) {
        if (!attempted || now - lastAttemptMs > RETRY_MS) connect();
        return;
    }
    if (!client.connected() || now - lastDataMs > 120000) {         // servers send a keep-alive every 20 s
        client.stop();
        loggedIn = false;
        statusText = T("Connection lost, retrying");
        lastAttemptMs = now;
        return;
    }
    for (int budget = 512; budget > 0 && client.available(); budget--) {
        char c = (char)client.read();
        lastDataMs = now;
        if (c == '\n') {
            if (!line.empty() && line[0] != '#') ingest(line, INTERNET);
            line.clear();
        } else if (c != '\r' && line.size() < 400) {
            line += c;
        }
    }
    if (config::get("aprs_beacon", "0") == "1" && (!beaconSent || now - lastBeaconMs > BEACON_MS)) {
        std::string error;
        sendBeacon(error);
        beaconSent = true;      // without a position, try again at the next interval
        lastBeaconMs = now;
    }
}

bool enabled() { return isEnabled; }
bool online() { return loggedIn; }
std::string status() { return statusText; }
const std::vector<Station>& stations() { return stationList; }
const std::vector<Message>& messages() { return messageList; }
int unread() { return unreadCount; }
void markRead() { unreadCount = 0; }

std::string beaconPacket(const std::string& path) {
    double lat, lon;
    if (myCall().empty() || !station::position(lat, lon)) return "";
    return aprs::header(myCall(), path) + aprs::formatPosition(lat, lon, '/', '[', config::get("aprs_comment", "HamOS"));
}

bool sendBeacon(std::string& error) {
    if (!canSend()) { error = statusText; return false; }
    std::string packet = beaconPacket("TCPIP*");
    if (packet.empty()) { error = T("No position: set a grid or wait for GPS"); return false; }
    sendPacket(packet.substr(packet.find(':') + 1));
    return true;
}

bool sendMessage(const std::string& to, const std::string& text, std::string& error) {
    if (!canSend()) { error = statusText; return false; }
    if (to.empty() || to.size() > 9 || text.empty()) { error = T("Type: CALLSIGN message"); return false; }
    if (text.size() > 67) { error = T("Message too long (67 characters)"); return false; }
    Message m;
    m.from = myCall();
    m.to = upper(to);
    m.text = text;
    m.id = std::to_string(nextId++);
    m.mine = true;
    sendPacket(aprs::formatMessage(m.to, m.text, m.id));
    addedCount++;
    if (messageList.size() >= MAX_MESSAGES) messageList.erase(messageList.begin());
    messageList.push_back(m);
    return true;
}

}  // namespace aprsis
