#include "LogCast.h"
#include <WiFiUdp.h>
#include "Config.h"
#include "Log.h"
#include "Net.h"
#include "lib/LogCast.h"

namespace logcast {

namespace {
unsigned count = 0;
}

void send(const adif::Qso& q) {
    std::string target = config::get("log_udp");
    size_t colon = target.rfind(':');
    if (target.empty() || colon == std::string::npos || !net::connected()) return;
    std::string host = target.substr(0, colon);
    uint16_t port = (uint16_t)atoi(target.c_str() + colon + 1);
    std::string body = config::get("log_udp_fmt", "n1mm") == "adif" ? adifRecord(q) : n1mm(q, config::get("callsign"));
    WiFiUDP udp;
    if (!udp.beginPacket(host.c_str(), port)) return;
    udp.write((const uint8_t*)body.data(), body.size());
    if (udp.endPacket()) count++;
}

void begin() { logbook::onAdd(send); }
unsigned sentCount() { return count; }

}  // namespace logcast
