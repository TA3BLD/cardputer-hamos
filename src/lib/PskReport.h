// Reception reports for PSK Reporter (pskreporter.info): who we decoded, as
// IPFIX datagrams sent to report.pskreporter.info, UDP port 4739.
//
// A datagram: IPFIX header, (sometimes) the two templates, our station record,
// then one record per decoded station. Fields of the PSK Reporter enterprise
// (30351): senderCallsign, frequency, sNR, mode, senderLocator,
// informationSource, plus the standard flowStartSeconds; receiverCallsign,
// receiverLocator, decodingSoftware for our station.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pskreport {

struct Spot {
    std::string call, locator, mode;    // mode as in ADIF: "FT8", "PSK31", "CW"
    uint32_t freqHz = 0;
    int snr = 0;
    uint32_t unixTime = 0;
};

constexpr uint16_t kReceiverTemplate = 0x50E3, kSenderTemplate = 0x50E2;
constexpr size_t kMaxDatagram = 1400;

namespace detail {
inline void u8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
inline void u16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
inline void u32(std::vector<uint8_t>& b, uint32_t v) { u16(b, v >> 16); u16(b, v & 0xFFFF); }
inline void str(std::vector<uint8_t>& b, const std::string& s) {
    size_t n = s.size() > 254 ? 254 : s.size();
    b.push_back((uint8_t)n);
    b.insert(b.end(), s.begin(), s.begin() + n);
}
inline void field(std::vector<uint8_t>& b, uint16_t id, uint16_t len) {     // enterprise field
    u16(b, 0x8000 | id);
    u16(b, len);
    u32(b, 30351);
}
inline void pad4(std::vector<uint8_t>& b, size_t setStart) {
    while ((b.size() - setStart) % 4) b.push_back(0);
    size_t len = b.size() - setStart;
    b[setStart + 2] = (uint8_t)(len >> 8);
    b[setStart + 3] = (uint8_t)(len & 0xFF);
}
}  // namespace detail

// Bytes of one spot record (for checking whether it still fits).
inline size_t recordSize(const Spot& s) {
    return 1 + s.call.size() + 4 + 1 + 1 + s.mode.size() + 1 + s.locator.size() + 1 + 4;
}

// Builds one datagram with as many of `spots` (from `first`) as fit; returns
// how many it took. `sequence` counts data records sent before (IPFIX rule).
inline size_t build(std::vector<uint8_t>& out, const std::vector<Spot>& spots, size_t first, bool withTemplates,
                    const std::string& myCall, const std::string& myLocator, const std::string& software,
                    uint32_t unixNow, uint32_t sequence, uint32_t domainId) {
    using namespace detail;
    out.clear();
    u16(out, 10);           // IPFIX version
    u16(out, 0);            // length, set below
    u32(out, unixNow);
    u32(out, sequence);
    u32(out, domainId);

    if (withTemplates) {
        size_t set = out.size();
        u16(out, 2);        // template set
        u16(out, 0);
        u16(out, kSenderTemplate);
        u16(out, 7);
        field(out, 1, 0xFFFF);      // senderCallsign
        field(out, 5, 4);           // frequency
        field(out, 6, 1);           // sNR
        field(out, 10, 0xFFFF);     // mode
        field(out, 3, 0xFFFF);      // senderLocator
        field(out, 11, 1);          // informationSource
        u16(out, 150);              // flowStartSeconds
        u16(out, 4);
        pad4(out, set);

        set = out.size();
        u16(out, 3);        // options template set
        u16(out, 0);
        u16(out, kReceiverTemplate);
        u16(out, 3);        // fields
        u16(out, 0);        // scope fields
        field(out, 2, 0xFFFF);      // receiverCallsign
        field(out, 4, 0xFFFF);      // receiverLocator
        field(out, 8, 0xFFFF);      // decodingSoftware
        pad4(out, set);
    }

    size_t set = out.size();
    u16(out, kReceiverTemplate);
    u16(out, 0);
    str(out, myCall);
    str(out, myLocator);
    str(out, software);
    pad4(out, set);

    size_t taken = 0;
    if (first < spots.size()) {
        set = out.size();
        u16(out, kSenderTemplate);
        u16(out, 0);
        for (size_t i = first; i < spots.size(); i++) {
            const Spot& s = spots[i];
            if (out.size() + recordSize(s) + 3 > kMaxDatagram) break;
            str(out, s.call);
            u32(out, s.freqHz);
            out.push_back((uint8_t)(int8_t)(s.snr < -128 ? -128 : s.snr > 127 ? 127 : s.snr));
            str(out, s.mode);
            str(out, s.locator);
            u8(out, 1);             // informationSource: decoded automatically
            u32(out, s.unixTime);
            taken++;
        }
        if (taken) pad4(out, set);
        else out.resize(set);
    }
    out[2] = (uint8_t)(out.size() >> 8);
    out[3] = (uint8_t)(out.size() & 0xFF);
    return taken;
}

}  // namespace pskreport
