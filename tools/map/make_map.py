"""Builds the offline map files HamOS reads from /HamOS/map on the SD card.

Inputs
  - Natural Earth country outlines (public domain), as TopoJSON from the npm
    package "world-atlas" 2.0.2: countries-110m.json, countries-50m.json,
    countries-10m.json
  - Cities from GeoNames (CC BY 4.0), as a table made from the npm package
    "all-the-cities" 3.1.0: name, country, population, lat, lon, feature code
    (one per line, tab separated; see README in sdcard/HamOS/map)

  python tools/map/make_map.py WORLD_ATLAS_DIR cities.tsv sdcard/HamOS/map

Outputs: lines0/1/2.bin (outlines at three levels of detail) and
cities0/1/2.bin, all in the same tiled format:

  header   "HMAP", version 1, level, tile size in degrees, unit exponent,
           columns u16, rows u16                                (12 bytes)
  index    columns*rows entries of (offset u32, size u32); tile (c, r) covers
           longitude -180 + c*tile .. and latitude -90 + r*tile ..
  lines    per record: n u16, kind u8 (0 coast, 1 border), 0 u8,
           lat0 i32, lon0 i32 (in units of 10^-unit degrees),
           then n x (dlat i16, dlon i16) relative to lat0/lon0
  cities   per record: lat i32, lon i32 (1e-5 degrees), rank u8
           (0 capital ... 4 small), population u32, name length u8, name
           (UTF-8 with the Turkish letters kept, other letters simplified);
           largest first within a tile
All numbers little-endian.
"""
import json
import math
import os
import struct
import sys
import unicodedata

atlas, cities_tsv, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)

LEVELS = [  # source, tile degrees, unit exponent, chunk size
    ("countries-110m.json", 180, 2, 200),
    ("countries-50m.json", 10, 3, 64),
    ("countries-10m.json", 2, 4, 48),
]


def arcs_of(path):
    topo = json.load(open(path))
    sx, sy = topo["transform"]["scale"]
    tx, ty = topo["transform"]["translate"]
    arcs = []
    for arc in topo["arcs"]:
        x = y = 0
        pts = []
        for dx, dy in arc:
            x += dx
            y += dy
            pts.append((y * sy + ty, x * sx + tx))      # (lat, lon)
        arcs.append(pts)
    # How many country shapes use each arc: 1 = coastline, 2 = border.
    uses = [0] * len(arcs)

    def walk(o):
        if isinstance(o, int):
            uses[o if o >= 0 else ~o] += 1
        elif isinstance(o, list):
            for x in o:
                walk(x)
    for geom in topo["objects"]["countries"]["geometries"]:
        walk(geom.get("arcs", []))
    return arcs, uses


def write_tiled(path, level, tile, unit, tiles):
    cols, rows = 360 // tile, 180 // tile
    header = b"HMAP" + bytes([1, level, tile, unit]) + struct.pack("<HH", cols, rows)
    index = bytearray()
    body = bytearray()
    base = len(header) + cols * rows * 8
    for r in range(rows):
        for c in range(cols):
            data = b"".join(tiles.get((c, r), []))
            index += struct.pack("<II", base + len(body) if data else 0, len(data))
            body += data
    open(path, "wb").write(header + index + body)
    return len(header) + len(index) + len(body)


def tile_of(lat, lon, tile):
    c = min(int((lon + 180) // tile), 360 // tile - 1)
    r = min(int((lat + 90) // tile), 180 // tile - 1)
    return max(c, 0), max(r, 0)


for level, (src, tile, unit, chunk) in enumerate(LEVELS):
    arcs, uses = arcs_of(os.path.join(atlas, src))
    scale = 10 ** unit
    limit = 32000 / scale                   # an int16 delta must reach every point of a chunk
    tiles = {}
    points = 0
    for arc, use in zip(arcs, uses):
        kind = 0 if use <= 1 else 1
        # Long straight edges get extra points so every chunk can make progress.
        dense = [arc[0]]
        for la, lo in arc[1:]:
            pla, plo = dense[-1]
            steps = int(max(abs(la - pla), abs(lo - plo)) / (limit / 2)) + 1
            for k in range(1, steps + 1):
                dense.append((pla + (la - pla) * k / steps, plo + (lo - plo) * k / steps))
        arc = dense
        i = 0
        while i < len(arc) - 1:
            # grow a chunk while it stays small enough
            j = i + 1
            la0, lo0 = arc[i]
            while j < len(arc) and j - i < chunk and abs(arc[j][0] - la0) < limit and abs(arc[j][1] - lo0) < limit:
                j += 1
            part = arc[i:j]
            if len(part) >= 2:
                q0 = (round(part[0][0] * scale), round(part[0][1] * scale))
                rec = struct.pack("<HBBii", len(part), kind, 0, q0[0], q0[1])
                for la, lo in part:
                    rec += struct.pack("<hh", round(la * scale) - q0[0], round(lo * scale) - q0[1])
                lats = [p[0] for p in part]
                lons = [p[1] for p in part]
                c0, r0 = tile_of(min(lats), min(lons), tile)
                c1, r1 = tile_of(max(lats), max(lons), tile)
                for c in range(c0, c1 + 1):
                    for r in range(r0, r1 + 1):
                        tiles.setdefault((c, r), []).append(rec)
                points += len(part)
            i = j - 1                           # chunks share their end point
    size = write_tiled(os.path.join(out, "lines%d.bin" % level), level, tile, unit, tiles)
    print("lines%d.bin: %d points, %d tiles, %.1f MB" % (level, points, len(tiles), size / 1e6))

# ---- cities ----
TURKISH = set("çÇğĞıİöÖşŞüÜ")


def simplify(name):
    out = ""
    for ch in name:
        if ch in TURKISH or (32 <= ord(ch) < 127):
            out += ch
        else:
            plain = "".join(c for c in unicodedata.normalize("NFKD", ch) if not unicodedata.combining(c))
            plain = {"ß": "ss", "ø": "o", "Ø": "O", "æ": "ae", "Æ": "AE", "ł": "l", "Ł": "L", "đ": "d", "Đ": "D",
                     "ð": "d", "þ": "th", "œ": "oe", "‘": "'", "’": "'", "ʻ": "'", "ʼ": "'"}.get(plain, plain)
            out += plain if all(32 <= ord(c) < 127 for c in plain) else ""
    return out.strip()


RANK = {"PPLC": 0, "PPLA": 1, "PPLA2": 2}
cities = []
for line in open(cities_tsv, encoding="utf-8"):
    name, country, pop, lat, lon, code = line.rstrip("\n").split("\t")
    name = simplify(name)
    if len(name) < 2 or len(name.encode()) > 40:
        continue
    pop = int(pop)
    rank = RANK.get(code, 3 if pop >= 10000 else 4)
    cities.append((float(lat), float(lon), rank, pop, name))

CITY_LEVELS = [  # tile degrees, who is included
    (180, lambda c: c[2] == 0 or c[3] >= 1000000),
    (10, lambda c: c[2] <= 1 or c[3] >= 50000),
    (2, lambda c: True),
]
for level, (tile, keep) in enumerate(CITY_LEVELS):
    tiles = {}
    chosen = sorted((c for c in cities if keep(c)), key=lambda c: (c[2] > 1, -c[3]))
    for lat, lon, rank, pop, name in chosen:
        raw = name.encode()
        rec = struct.pack("<iiBIB", round(lat * 1e5), round(lon * 1e5), rank, pop, len(raw)) + raw
        key = tile_of(lat, lon, tile)
        tiles.setdefault(key, []).append(rec)
    size = write_tiled(os.path.join(out, "cities%d.bin" % level), level, tile, 5, tiles)
    print("cities%d.bin: %d cities, %.1f MB" % (level, len(chosen), size / 1e6))
