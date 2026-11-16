"""Broad Street Line strip map (line map style) for the portrait e-paper screen.

    python -I tools/subway_map/make_bsl.py [--preview out.png]

Reads septa_subway.json (from extract.py) and writes firmware/Display/subway_bsl.h.

Layout: stations evenly spaced top (Fern Rock) to bottom (NRG). Three strands show the three services:
B1 local (right), B2 express (middle), B3 Broad-Ridge Spur (left, which leaves at Fairmount for Chinatown and
8th-Market). A ring on a strand means that service stops there; every stop pattern comes from SEPTA's schedules.
Under each name: rail/trolley connections, then bus routes stopping within 150 m (400-series school routes left out).
"""
import argparse, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
PW, PH = 480, 1360
FD = Path("/usr/share/fonts/truetype/crosextra")
def font(sz, bold=True): return ImageFont.truetype(str(FD / ("Carlito-Bold.ttf" if bold else "Carlito-Regular.ttf")), sz)
F_NAME, F_CONN, F_BUS, F_KEY, F_KEYB = font(21), font(14), font(14, False), font(14, False), font(14)
F_TITLE, F_TITLE2, F_BADGE = font(30), font(17, False), font(30)
CLOCK_BOX = (232, 6, 474, 92)          # filled by the firmware (same as the full map)
FOOTER_Y = 1318

data = json.loads((HERE / "septa_subway.json").read_text())
B1 = [s["name"] for s in data["lines"]["B1"]]
B2 = {s["name"] for s in data["lines"]["B2"]}
B3 = [s["name"] for s in data["lines"]["B3"]]
CONN = data["bsl_connections"]
PATCO = {"Walnut-Locust": "PATCO (15-16th & Locust)", "8th-Market": "PATCO"}
NAMES = {"Fern Rock Transit Center": "Fern Rock T.C.", "Olney Transit Center": "Olney T.C.", "Broad-Allegheny": "Allegheny",
         "Broad-Girard": "Girard", "Broad-Spring Garden": "Spring Garden", "15th St/City Hall": "City Hall",
         "NRG Station": "NRG Station"}

img = Image.new("1", (PW, PH), 1)
d = ImageDraw.Draw(img)
mask = Image.new("1", (PW, PH), 0); dm = ImageDraw.Draw(mask)
labels = []
def text(x, y, s, f, anchor="la", name=None):
    d.text((x, y), s, font=f, anchor=anchor, fill=0)
    labels.append((name or s, d.textbbox((x, y), s, font=f, anchor=anchor)))
def line(pts, w, fill=0):
    d.line(pts, fill=fill, width=w, joint="curve")
    if fill == 0: dm.line(pts, fill=1, width=w)
def dashed(p0, p1, w, on=8, off=6):
    (x0, y0), (x1, y1) = p0, p1
    n = max(abs(x1 - x0), abs(y1 - y0)); t = 0
    while t < n:
        t1 = min(t + on, n)
        line([(x0 + (x1 - x0) * t / n, y0 + (y1 - y0) * t / n), (x0 + (x1 - x0) * t1 / n, y0 + (y1 - y0) * t1 / n)], w)
        t = t1 + off
def ring(x, y, r=8):
    d.ellipse([x - r, y - r, x + r, y + r], fill=0); d.ellipse([x - r + 3, y - r + 3, x + r - 3, y + r - 3], fill=1)
    dm.ellipse([x - r, y - r, x + r, y + r], fill=1)

# ---------- names and connections ----------
def conn_lines(n):
    c = CONN[n]; out = []
    metro = []
    if "L1" in c["metro"]: metro.append("L Market-Frankford")
    if any(m.startswith("T") for m in c["metro"]): metro.append("T1-T5 trolleys")
    if "G1" in c["metro"]: metro.append("G1 Girard trolley")
    for r in c["rail"]:
        if r == "Fern Rock Transit Center": metro.append("Regional Rail")
        elif r == "Suburban Station": metro.append("Regional Rail (Suburban Sta.)")
        elif r == "North Broad": metro.append("Regional Rail (North Broad)")
        elif "Amtrak" in r: metro.append("Amtrak")
    if n in PATCO: metro.append(PATCO[n])
    if n == "15th St/City Hall": metro = ["L Market-Frankford, T1-T5 trolleys,", "Regional Rail (Suburban Sta.)"]
    if n in ("15th St/City Hall", "8th-Market"): out += [(m, F_CONN) for m in metro]
    elif metro: out.append((", ".join(metro), F_CONN))
    if c["bus"]: out.append(("Bus " + " ".join(c["bus"]), F_BUS))
    return out

def wrap(s, f, width):
    words, rows, cur = s.split(" "), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if f.getlength(t) <= width or not cur: cur = t
        else: rows.append(cur); cur = w
    rows.append(cur); return rows

# ---------- geometry ----------
X3, X2, X1 = 184, 206, 228            # strands: B3 spur, B2 express, B1 local
XS = 140                              # the spur after Fairmount
XN = 246                              # station names
Y0, Y1 = 150, 1270                    # Fern Rock, NRG

# Row spacing: each station gets the height its connection lines need, the rest is shared out evenly.
need = []
for n in B1[:-1]:
    k = sum(len(wrap(t, f, PW - XN - 8)) for t, f in conn_lines(n))
    need.append(26 + 14 * k)
extra = (Y1 - Y0 - sum(need)) / len(need)
assert extra >= 0, "stations do not fit"
row, y = {}, Y0
for n, h in zip(B1, need + [0]):
    row[n] = y; y += h + extra
row["Chinatown"] = row["Race-Vine"]   # the spur stations sit level with their real neighbours on Broad St
row["8th-Market"] = row["15th St/City Hall"]
W = 7                                 # strand width

ROW_LINES = {}   # filled below; rows are spaced by how many connection lines each station has
# B1: Fern Rock to NRG, stops everywhere.
line([(X1, row[B1[0]]), (X1, row[B1[-1]])], W)
# B2: Fern Rock to Walnut-Locust; some trains continue non-stop to NRG (dashed).
line([(X2, row[B1[0]]), (X2, row["Walnut-Locust"])], W)
dashed((X2, row["Walnut-Locust"] + 10), (X2, row["NRG Station"]), W - 2)
# B3: Olney (some trains from Fern Rock: dashed) down Broad to Fairmount, then the spur.
dashed((X3, row[B1[0]]), (X3, row["Olney Transit Center"]), W - 2)
yf = row["Fairmount"]
line([(X3, row["Olney Transit Center"]), (X3, yf), (XS, yf + 40), (XS, row["8th-Market"])], W)

# Stops: a ring where each service stops.
for n in B1: ring(X1, row[n])
for n in B2: ring(X2, row[n])
for n in B3: ring(X3 if n not in ("Chinatown", "8th-Market") else XS, row[n])
ring(X2, row["NRG Station"])                            # the B2 trains that run on
ring(X3, row["Fern Rock Transit Center"])               # the B3 trains that start at Fern Rock
# A thin bar joining each station's rings to its name.
for n in B1:
    stops = [x for x, s in ((X3, n in B3), (X2, n in B2), (X1, True)) if s]
    d.line([(min(stops), row[n]), (XN - 6, row[n])], fill=0, width=2)
    for x in stops: ring(x, row[n])

for n in B1:
    y = row[n]
    text(XN, y - 13, NAMES.get(n, n), F_NAME, "la", name=n)
    yy = y + 8
    for s, f in conn_lines(n):
        for r in wrap(s, f, PW - XN - 8):
            text(XN + 1, yy, r, f, "la", name=n + " conn"); yy += 14
for n in ("Chinatown", "8th-Market"):
    y = row[n]
    text(XS - 13, y - 13, n, F_NAME, "ra", name=n)
    yy = y + 8
    for s, f in conn_lines(n):
        for r in wrap(s, f, XS - 14):
            text(XS - 13, yy, r, f, "ra", name=n + " conn"); yy += 14

# Strand headings above Fern Rock.
for x, s in ((X3, "B3"), (X2, "B2"), (X1, "B1")):
    d.rounded_rectangle([x - 11, Y0 - 44, x + 11, Y0 - 26], radius=4, fill=0)
    d.text((x, Y0 - 35), s, font=F_KEYB, anchor="mm", fill=1)
    labels.append((s + " head", (x - 11, Y0 - 44, x + 11, Y0 - 26)))

# ---------- header ----------
d.ellipse([14, 10, 66, 62], fill=0); d.text((40, 37), "B", font=F_BADGE, anchor="mm", fill=1)
d.text((76, 12), "Broad Street", font=F_TITLE, fill=0)
d.text((76, 44), "Line", font=F_TITLE, fill=0)
d.text((15, 72), "Fern Rock T.C. - NRG Station", font=F_TITLE2, fill=0)

# ---------- key (left column, above the spur) ----------
KX = 14
key = [("B1 Local", True), ("every station", False), ("", False), ("B2 Express", True), ("Fern Rock - Walnut-", False),
       ("Locust. Some trains", False), ("run on to NRG", False), ("(dashed).", False), ("", False),
       ("B3 Broad-Ridge Spur", True), ("Olney (some from", False), ("Fern Rock) to", False), ("8th-Market.", False), ("", False),
       ("Ring = the train", True), ("stops here.", True)]
ky = 140
for s, bold in key:
    if s: text(KX, ky, s, F_KEYB if bold else F_KEY, "la", name="key " + s)
    ky += 16
notes = ["Overnight (midnight-", "5 AM): B1 OWL buses", "along Broad St.", "",
         "Bus routes stopping", "within 150 m.", "School routes (400s)", "not shown.", "",
         "Source: SEPTA", "schedule data,", "Nov 2026."]
ky = max(b[3] for nm, b in labels if nm.startswith("8th-Market")) + 24
for s in notes:
    if s: text(KX, ky, s, F_KEY, "la", name="note " + s)
    ky += 16

d.line([(14, FOOTER_Y - 4), (PW - 14, FOOTER_Y - 4)], fill=0, width=2)

# ---------- checks ----------
def overlap(a, b): return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
problems = []
for i, (na, a) in enumerate(labels):
    if a[0] < 0 or a[2] > PW: problems.append(f"off canvas: {na}")
    for nb, b in labels[i + 1:]:
        if overlap(a, b): problems.append(f"overlap: {na} / {nb}")
    if not na.endswith("head") and mask.crop(tuple(int(v) for v in a)).getbbox(): problems.append(f"on a line: {na}")
    if overlap(a, CLOCK_BOX) or a[3] > FOOTER_Y - 6: problems.append(f"reserved box: {na}")
print("\n".join(problems) if problems else "no overlaps")

ap = argparse.ArgumentParser(); ap.add_argument("--preview"); args = ap.parse_args()
if args.preview: img.convert("L").save(args.preview)
px = img.load(); raw = bytearray()
for y in range(PH):
    for bx in range(PW // 8):
        b = 0
        for k in range(8):
            if px[bx * 8 + k, y] == 0: b |= 0x80 >> k
        raw.append(b)
enc = bytearray(); i = 0
while i < len(raw):
    j = i
    while j < len(raw) and j - i < 255 and raw[j] == raw[i]: j += 1
    enc += bytes([j - i, raw[i]]); i = j
out = ROOT / "firmware/Display/subway_bsl.h"
out.write_text(f"""// Generated by tools/subway_map/make_bsl.py from {data['source']}. Do not edit by hand.
// Broad Street Line strip map, portrait {PW} x {PH}, same format as subway_map.h. {len(enc):,} bytes packed.
#pragma once
#include <stdint.h>
namespace septa_bsl {{
constexpr uint32_t RLE_BYTES = {len(enc)};
static const uint8_t RLE[{len(enc)}] PROGMEM = {{{",".join(str(b) for b in enc)}}};
}}
""")
print(f"wrote {out.relative_to(ROOT)} ({len(enc):,} bytes packed)")
