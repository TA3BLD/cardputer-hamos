#!/usr/bin/env python3
"""Decodes a HamOS PSK Reporter datagram with a generic IPFIX reader (RFC 7011)
and checks the content. Usage:
  g++ -std=c++17 -I src test/host/test_web.cpp -o test/host/t.out
  python3 tools/pskreporter/check_ipfix.py "$(test/host/t.out ipfix)"
"""
import struct
import sys

ENT = 30351
NAMES = {(ENT, 1): "senderCallsign", (ENT, 2): "receiverCallsign", (ENT, 3): "senderLocator",
         (ENT, 4): "receiverLocator", (ENT, 5): "frequency", (ENT, 6): "sNR", (ENT, 8): "decodingSoftware",
         (ENT, 10): "mode", (ENT, 11): "informationSource", (0, 150): "flowStartSeconds"}


def decode(data):
    version, length, export, seq, domain = struct.unpack(">HHIII", data[:16])
    assert version == 10, version
    assert length == len(data), (length, len(data))
    templates, records = {}, []
    pos = 16
    while pos < length:
        set_id, set_len = struct.unpack(">HH", data[pos:pos + 4])
        assert set_len >= 4 and pos + set_len <= length
        body = data[pos + 4:pos + set_len]
        if set_id in (2, 3):
            p = 0
            while p + 4 <= len(body):
                tid, count = struct.unpack(">HH", body[p:p + 4])
                if tid == 0:
                    break      # padding
                p += 4
                if set_id == 3:
                    scope = struct.unpack(">H", body[p:p + 2])[0]
                    assert scope == 0
                    p += 2
                fields = []
                for _ in range(count):
                    fid, flen = struct.unpack(">HH", body[p:p + 4])
                    p += 4
                    ent = 0
                    if fid & 0x8000:
                        ent = struct.unpack(">I", body[p:p + 4])[0]
                        p += 4
                        fid &= 0x7FFF
                    fields.append((NAMES[(ent, fid)], flen))
                templates[tid] = fields
        else:
            fields = templates[set_id]
            p = 0
            while p < len(body):
                if all(b == 0 for b in body[p:]):
                    break      # padding
                rec = {}
                for name, flen in fields:
                    if flen == 0xFFFF:
                        n = body[p]
                        p += 1
                        rec[name] = body[p:p + n].decode()
                        p += n
                    else:
                        raw = body[p:p + flen]
                        p += flen
                        v = int.from_bytes(raw, "big", signed=(name == "sNR"))
                        rec[name] = v
                records.append(rec)
        pos += set_len
    return export, domain, records


export, domain, recs = decode(bytes.fromhex(sys.argv[1]))
assert export == 1700000100 and domain == 0x12345678
station = recs[0]
assert station == {"receiverCallsign": "TA3BLD", "receiverLocator": "KM38NK", "decodingSoftware": "HamOS 0.8.0"}, station
spots = recs[1:]
assert len(spots) == 3, spots
assert spots[0] == {"senderCallsign": "DL1ABC", "frequency": 14075123, "sNR": -12, "mode": "FT8",
                    "senderLocator": "JO31", "informationSource": 1, "flowStartSeconds": 1700000000}, spots[0]
assert spots[1]["senderLocator"] == "" and spots[1]["sNR"] == 3
assert spots[2]["mode"] == "PSK31" and spots[2]["frequency"] == 7040500 and spots[2]["sNR"] == -20
print("IPFIX datagram decodes correctly:", len(spots), "spots")
