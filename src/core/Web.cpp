#include "Web.h"
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "Config.h"
#include "Log.h"
#include "Net.h"
#include "Storage.h"
#include "Updater.h"
#include "Usb.h"
#include "WebPage.h"

namespace web {

namespace {

WebServer server(80);
bool started = false;

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
           "<nav><a href=/>Control panel</a><a href=/log.adi>Download log</a>"
           "<a href=/update>Update</a></nav><h2>" + title + "</h2>" +
           body + "</body></html>";
}

void sendPage(const std::string& title, const std::string& body) {
    std::string html = page(title, body);
    server.send_P(200, "text/html", html.data(), html.size());
}

// The control page (tools/pc/hamos.html, embedded by tools/pc/embed.py).
void handleRoot() {
    if (!allowed()) return;
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", (const char*)kPageGz, kPageGzLen);
}

// Only the page this device served may use the API: a page from another site
// open in the same browser must not reach it (the browser sends Origin).
bool sameOrigin() {
    std::string o = server.header("Origin").c_str(), host = server.hostHeader().c_str();
    if (o.empty()) return true;
    size_t p = o.find("://");
    if (p != std::string::npos) o = o.substr(p + 3);
    return o == host;
}

// The same requests as over USB (see Usb.h), one JSON object per POST.
void handleApi() {
    if (!allowed()) return;
    if (!sameOrigin()) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }
    std::string reply = usb::handle(server.arg("plain").c_str(), true);
    if (reply.empty()) reply = "{\"ok\":false,\"error\":\"no reply\"}";
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/json", reply.data(), reply.size());    // send() would copy it into a String
}

// Screen drawing steps since the last call, while the page mirrors the screen.
void handleScreen() {
    if (!allowed()) return;
    server.sendHeader("Cache-Control", "no-store");
    std::string ops = usb::webScreen();
    server.send_P(200, "text/plain", ops.data(), ops.size());
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

// ---- firmware update from the browser ----
// Only offered when a Web UI password is set: anyone who can reach this page
// could otherwise replace the firmware.
bool updateOk = false;
std::string updateError;

void handleUpdatePage() {
    if (!allowed()) return;
    if (config::get("web_pass").empty()) {
        sendPage("Firmware update", "<p>Set a <b>Web UI password</b> in Settings on the device first. "
                                    "Updating is only allowed with a password.</p>");
        return;
    }
    sendPage("Firmware update",
             "<p>Choose <code>hamOS_&lt;version&gt;.bin</code> from a release (not <code>hamOS_0x0.bin</code>), or the "
             "<code>firmware.bin</code> built by PlatformIO (<code>.pio/build/cardputer-adv/firmware.bin</code>). "
             "The device restarts when it is done.</p>"
             "<form method=post action=/update enctype=multipart/form-data><input type=file name=firmware accept=.bin> "
             "<input type=submit value=Install></form>");
}

void handleUpdateUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        std::string pass = config::get("web_pass");
        updateOk = false;
        if (pass.empty() || !server.authenticate("hamos", pass.c_str())) updateError = "Not authorised";
        else if (!Update.begin(UPDATE_SIZE_UNKNOWN)) updateError = "Could not start the update";
        else updateOk = true;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (updateOk && Update.write(up.buf, up.currentSize) != up.currentSize) {
            updateOk = false;
            updateError = "Writing the firmware failed";
            Update.abort();
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (updateOk && !Update.end(true)) {
            updateOk = false;
            updateError = "The file is not a valid firmware image";
        }
    } else if (updateOk) {      // upload aborted
        Update.abort();
        updateOk = false;
        updateError = "Upload interrupted";
    }
}

void handleUpdateDone() {
    if (!allowed()) return;
    if (!updateOk) {
        sendPage("Firmware update", "<p>Update failed: " + esc(updateError) + ". The old firmware is still installed.</p>");
        return;
    }
    updater::afterManualUpdate();       // goes back to this version if the new one fails to start
    sendPage("Firmware update", "<p>Installed. HamOS is restarting...</p>");
    delay(500);
    ESP.restart();
}

}  // namespace

void tick() {
    bool wanted = config::get("web_on", "0") == "1";
    if (started && !wanted) {           // switched off in WiFi / Settings
        server.stop();
        started = false;
        return;
    }
    if (!started) {
        if (!wanted || !net::connected()) return;
        static bool routes = false;
        if (!routes) {
            static const char* headers[] = {"Origin"};
            server.collectHeaders(headers, 1);
            server.on("/", HTTP_GET, handleRoot);
            server.on("/api", HTTP_POST, handleApi);
            server.on("/screen", HTTP_GET, handleScreen);
            server.on("/log.adi", HTTP_GET, handleLog);
            server.on("/update", HTTP_GET, handleUpdatePage);
            server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
            routes = true;
        }
        server.begin();
        started = true;
    }
    server.handleClient();
}

bool running() { return started && net::connected(); }
std::string address() { return running() ? "http://" + net::address() : ""; }

}  // namespace web
