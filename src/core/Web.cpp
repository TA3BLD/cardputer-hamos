#include "Web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ctime>
#include "Config.h"
#include "Log.h"
#include "Mesh.h"
#include "Net.h"
#include "Station.h"
#include "Storage.h"

namespace web {

namespace {

WebServer server(80);
bool started = false;
bool restartWifi = false;   // apply new WiFi settings after the reply has gone out

std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

bool allowed() {
    std::string pass = config::get("web_pass");
    if (pass.empty() || server.authenticate("hamos", pass.c_str())) return true;
    server.requestAuthentication();
    return false;
}

std::string page(const std::string& title, const std::string& body) {
    return "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>HamOS - " + title + "</title><style>body{font-family:sans-serif;max-width:40em;margin:1em auto;padding:0 1em}"
           "input{font-size:1em;padding:.3em;margin:.2em 0;box-sizing:border-box;max-width:100%}label{display:block;margin-top:.6em}"
           "nav a{margin-right:1em}.me{color:#070}.dim{color:#777}</style></head><body>"
           "<nav><a href=/>Status</a><a href=/settings>Settings</a><a href=/log.adi>Download log</a></nav><h2>" + title + "</h2>" +
           body + "</body></html>";
}

void sendPage(const std::string& title, const std::string& body) { server.send(200, "text/html", page(title, body).c_str()); }

void handleRoot() {
    if (!allowed()) return;
    std::string b = "<p><b>" + esc(config::get("callsign", "(no callsign)")) + "</b> &middot; grid " + esc(station::grid()) +
                    " &middot; " + std::to_string(logbook::total()) + " QSOs in the log</p>";
    b += "<h3>Mesh</h3>";
    if (!mesh::ready()) {
        b += "<p class=dim>" + esc(mesh::status()) + "</p>";
    } else {
        mesh::Stats s = mesh::stats();
        char line[96];
        snprintf(line, sizeof(line), "%s on %.3f MHz, %d nodes heard", s.channel.c_str(), s.freqMhz, (int)mesh::nodes().size());
        b += std::string("<p class=dim>") + esc(line) + "</p>";
        for (const mesh::Message& m : mesh::messages()) {
            char stamp[8] = "--:--";
            if (m.utc) {
                struct tm t;
                gmtime_r(&m.utc, &t);
                strftime(stamp, sizeof(stamp), "%H:%M", &t);
            }
            b += std::string("<div") + (m.mine ? " class=me" : "") + "><span class=dim>" + stamp + "</span> <b>" +
                 esc(mesh::nodeName(m.from)) + "</b>: " + esc(m.text) + "</div>";
        }
        b += "<form method=post action=/send><input name=text maxlength=200 size=30 autofocus> <input type=submit value=Send></form>";
    }
    sendPage("Status", b);
}

void handleSend() {
    if (!allowed()) return;
    std::string error;
    if (!mesh::sendText(server.arg("text").c_str(), error)) {
        sendPage("Not sent", "<p>" + esc(error) + "</p><p><a href=/>Back</a></p>");
        return;
    }
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
}

struct WebField {
    const char* key;
    const char* label;
};
const WebField kFields[] = {
    {"callsign", "Callsign"}, {"grid", "Grid locator"}, {"utc_offset", "UTC offset (hours)"},
    {"mesh_name", "Mesh long name"}, {"mesh_short", "Mesh short name (4)"}, {"mesh_region", "Mesh region (EU_868, EU_433, US)"},
    {"mesh_preset", "Mesh preset"}, {"mesh_channel", "Mesh channel name"}, {"mesh_pos", "Share position (0/1)"},
    {"dx_host", "DX cluster host"}, {"dx_port", "DX cluster port"}, {"cw_wpm", "CW speed (WPM)"},
};

void handleSettings() {
    if (!allowed()) return;
    std::string b = "<form method=post action=/settings>";
    for (const WebField& f : kFields)
        b += std::string("<label>") + f.label + "<br><input name=" + f.key + " value=\"" + esc(config::get(f.key)) + "\"></label>";
    b += "<label>WiFi name<br><input name=wifi_ssid value=\"" + esc(config::get("wifi_ssid")) + "\"></label>";
    b += "<label>WiFi password (leave empty to keep)<br><input name=wifi_pass type=password></label>";
    b += "<p><input type=submit value=Save></p><p class=dim>Empty fields use the built-in default.</p></form>";
    sendPage("Settings", b);
}

void handleSettingsSave() {
    if (!allowed()) return;
    for (const WebField& f : kFields) {
        if (!server.hasArg(f.key)) continue;
        std::string v = server.arg(f.key).c_str();
        if (v.empty()) config::erase(f.key);
        else config::set(f.key, v);
    }
    std::string ssid = server.arg("wifi_ssid").c_str(), pass = server.arg("wifi_pass").c_str();
    if (ssid != config::get("wifi_ssid") || !pass.empty()) {
        if (ssid.empty()) config::erase("wifi_ssid");
        else config::set("wifi_ssid", ssid);
        if (!pass.empty()) config::set("wifi_pass", pass);
        restartWifi = true;
    }
    bool saved = config::save();
    mesh::begin();
    sendPage("Settings", saved ? "<p>Saved.</p><p><a href=/>Back</a></p>"
                               : "<p>Applied, but there is no SD card: lost at power-off.</p><p><a href=/>Back</a></p>");
}

void handleLog() {
    if (!allowed()) return;
    if (!storage::exists(logbook::kPath)) {
        server.send(404, "text/plain", "The log is empty.");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=hamos-log.adi");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/plain", "");
    storage::readChunks(logbook::kPath, [](const char* data, size_t len, void*) { server.sendContent(data, len); }, nullptr);
    server.sendContent("");
}

}  // namespace

void tick() {
    if (!started) {
        if (config::get("web_on", "0") != "1" || !net::connected()) return;
        server.on("/", HTTP_GET, handleRoot);
        server.on("/send", HTTP_POST, handleSend);
        server.on("/settings", HTTP_GET, handleSettings);
        server.on("/settings", HTTP_POST, handleSettingsSave);
        server.on("/log.adi", HTTP_GET, handleLog);
        server.begin();
        started = true;
    }
    server.handleClient();
    if (restartWifi) {
        restartWifi = false;
        delay(200);         // let the reply leave before the connection drops
        net::begin();
    }
}

bool running() { return started && net::connected(); }
std::string address() { return running() ? std::string("http://") + WiFi.localIP().toString().c_str() : ""; }

}  // namespace web
