#include "Updater.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "mbedtls/sha256.h"
#include "Config.h"
#include "Http.h"
#include "Lang.h"
#include "Net.h"
#include "Version.h"

namespace updater {

namespace {

constexpr const char* kNs = "hamos_upd";
constexpr uint32_t kStableMs = 60000;       // running this long counts as a good start
constexpr uint32_t kBadStarts = 3;
constexpr uint32_t kCheckEveryMs = 24UL * 3600 * 1000;

Status st;
bool watching = false;
std::string rolledBack;
uint32_t lastCheckMs = 0;
bool checkedOnce = false;
uint32_t wifiSinceMs = 0;

std::string repo() { return config::get("update_repo", "TA3BLD/cardputer-hamos"); }
std::string token() { return config::get("update_token"); }

void addAuth(http::Request& r) {
    if (!token().empty()) r.headers.push_back({"Authorization", "Bearer " + token()});
}

struct Download {
    mbedtls_sha256_context sha;
    size_t done = 0;
    long total = 0;
    bool writeFailed = false;
};

void (*progressHook)() = nullptr;

}  // namespace

void bootGuard() {
    Preferences p;
    p.begin(kNs, false);
    watching = p.getBool("watch", false);
    std::string undone = p.getString("undone", "").c_str();
    if (!undone.empty()) {                  // we are the old version, brought back
        rolledBack = undone;
        p.remove("undone");
    }
    if (watching) {
        uint32_t starts = p.getUInt("starts", 0) + 1;
        p.putUInt("starts", starts);
        if (starts >= kBadStarts) {
            watching = false;
            p.putBool("watch", false);
            p.putUInt("starts", 0);
            const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
            esp_app_desc_t desc;
            if (other && esp_ota_get_partition_description(other, &desc) == ESP_OK &&
                esp_ota_set_boot_partition(other) == ESP_OK) {
                p.putString("undone", HAMOS_VERSION);
                p.end();
#ifndef HAMOS_SIM
                ESP.restart();
#endif
                return;
            }
        }
    }
    p.end();
}

void afterManualUpdate() {
    Preferences p;
    p.begin(kNs, false);
    p.putBool("watch", true);
    p.putUInt("starts", 0);
    p.end();
}

std::string rolledBackFrom() { return rolledBack; }
void onProgress(void (*hook)()) { progressHook = hook; }

void restartNow() {
#ifndef HAMOS_SIM
    delay(1500);
    ESP.restart();
#endif
}
const Status& status() { return st; }

bool check() {
    st.state = State::Checking;
    st.message = T("Checking for updates...");
    lastCheckMs = millis();
    checkedOnce = true;
    http::Request r;
    r.url = "https://api.github.com/repos/" + repo() + "/releases/latest";
    r.headers.push_back({"Accept", "application/vnd.github+json"});
    r.verify = true;
    r.maxBytes = 64 * 1024;
    addAuth(r);
    http::Response a = http::fetch(r);
    if (a.status == 404 || a.status == 401) {
        st.state = State::Failed;
        st.message = T("Release not found (private repository? set Update token)");
        return false;
    }
    if (!a.ok()) {
        st.state = State::Failed;
        st.message = a.error;
        return false;
    }
    release::Info info;
    if (!release::parse(a.body, info)) {
        st.state = State::Failed;
        st.message = T("Release not found (private repository? set Update token)");
        return false;
    }
    if (release::compareVersions(info.version, HAMOS_VERSION) <= 0) {
        st.state = State::UpToDate;
        st.message = T("This is the newest version");
        return true;
    }
    st.latest = info;
    st.state = State::Available;
    st.message = T("Update available");
    return true;
}

bool install() {
    if (st.state != State::Available) return false;
    const release::Info& info = st.latest;
    auto fail = [](const std::string& why) {
        st.state = State::Failed;
        st.message = why;
        return false;
    };
    if (!net::connected()) return fail(T("No WiFi"));
    int battery = M5Cardputer.Power.getBatteryLevel();
    if (battery >= 0 && battery < 30) return fail(T("Charge the battery first (at least 30%)"));
    if (!Update.begin(info.size ? info.size : UPDATE_SIZE_UNKNOWN)) return fail(T("Not enough room for the update"));

    st.state = State::Installing;
    st.percent = 0;
    st.message = T("Installing - do not switch off");
    http::Request r;
    r.verify = true;
    r.timeoutMs = 20000;
    if (!token().empty() && !info.assetApiUrl.empty()) {     // private repository: through the API
        r.url = info.assetApiUrl;
        r.headers.push_back({"Accept", "application/octet-stream"});
        addAuth(r);
    } else {
        r.url = info.downloadUrl;
    }
    Download d;
    mbedtls_sha256_init(&d.sha);
    mbedtls_sha256_starts(&d.sha, 0);
    http::Response res = http::stream(
            r,
            [](const uint8_t* data, size_t n, void* ctx) {
                Download* d = (Download*)ctx;
                if (Update.write((uint8_t*)data, n) != n) {
                    d->writeFailed = true;
                    return false;
                }
                mbedtls_sha256_update(&d->sha, data, n);
                d->done += n;
                int pct = d->total > 0 ? (int)(d->done * 100 / (size_t)d->total) : 0;
                if (pct != st.percent) {
                    st.percent = pct;
                    if (progressHook) progressHook();
                }
                return true;
            },
            &d, [](long size, void* ctx) { ((Download*)ctx)->total = size; });
    unsigned char sum[32];
    mbedtls_sha256_finish(&d.sha, sum);
    mbedtls_sha256_free(&d.sha);
    if (!res.ok() || d.writeFailed) {
        Update.abort();
        return fail(d.writeFailed ? T("Not enough room for the update") : res.error);
    }
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", sum[i]);
    bool sizeOk = !info.size || d.done == info.size;
    bool shaOk = info.sha256.empty() || info.sha256 == hex;
    if (!sizeOk || !shaOk) {
        Update.abort();
        return fail(T("The download does not match the release"));
    }
    if (!Update.end(true)) return fail(T("The file is not a valid firmware image"));
    afterManualUpdate();
    st.state = State::Installed;
    st.percent = 100;
    st.message = T("Installed. Restarting...");
    return true;
}

void tick(bool idle) {
    uint32_t now = millis();
    if (watching && now > kStableMs) {          // the new version runs: keep it
        watching = false;
        Preferences p;
        p.begin(kNs, false);
        p.putBool("watch", false);
        p.putUInt("starts", 0);
        p.end();
    }
    if (!net::connected()) {
        wifiSinceMs = 0;
        return;
    }
    if (!wifiSinceMs) wifiSinceMs = now ? now : 1;
    if (config::get("update_check", "1") != "1" || !idle) return;
    if (st.state == State::Checking || st.state == State::Installing || st.state == State::Installed) return;
    bool due = !checkedOnce ? now - wifiSinceMs > 20000 : now - lastCheckMs > kCheckEveryMs;
    if (due) check();
}

}  // namespace updater
