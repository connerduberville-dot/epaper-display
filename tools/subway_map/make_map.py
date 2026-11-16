"""Draw the SEPTA subway map for the portrait (480 x 1360) e-paper screen.

    python -I tools/subway_map/make_map.py [--preview out.png]

Reads septa_subway.json (SEPTA's GTFS stations, made by extract.py) and writes:
  firmware/Display/subway_map.h   the 1-bit map, run-length encoded, in portrait orientation
  (optional) a PNG preview

How the geometry is made "accurate but fits 480 px":
  1. Stations are placed from their real coordinates, rotated 9.5 degrees so Broad Street is vertical (Market Street
     then comes out horizontal).
  2. Each axis is stretched piecewise (monotonic): Center City gets more room, West Philly and Kensington less.
     Every station keeps its true north-south and east-west order relative to every other station.
  3. Lines are drawn station to station. The Broad-Ridge Spur is drawn as a right angle instead of its real
     diagonal under Ridge Avenue, so the station names inside it stay readable.
The board draws the clock (top right) and the status footer itself; those boxes are left blank here.
"""
import argparse, json, math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
PW, PH = 480, 1360                      # portrait canvas
FONT_DIR = Path("/usr/share/fonts/truetype/crosextra")
def font(sz, bold=True):
    return ImageFont.truetype(str(FONT_DIR / ("Carlito-Bold.ttf" if bold else "Carlito-Regular.ttf")), sz)
F_ST = font(16)          # station names
F_SM = font(14)          # trolley / PATCO / notes
F_XS = font(13, False)
F_TITLE = font(34)
F_SUB = font(15, False)
F_BADGE = font(17)

# Boxes the firmware fills (portrait coordinates). Keep in sync with subway.h.
CLOCK_BOX = (232, 6, 474, 92)     # x0, y0, x1, y1
FOOTER_Y = 1318

data = json.loads((HERE / "septa_subway.json").read_text())
L = data["lines"]

# ---------- 1. rotate so Broad Street is vertical ----------
LA0, LO0 = 39.95247, -75.16413           # 15th St/City Hall
TH = math.atan2((-75.1448 + 75.1739) * 85.2, (40.039 - 39.9054) * 111.0)
def rot(lat, lon):
    x, y = (lon - LO0) * 85.2, (lat - LA0) * 111.0
    return x * math.cos(TH) - y * math.sin(TH), x * math.sin(TH) + y * math.cos(TH)   # (u east, v north) km

# ---------- 2. piecewise stretch ----------
def interp(knots, t):
    for (a, pa), (b, pb) in zip(knots, knots[1:]):
        if a <= t <= b: return pa + (t - a) / (b - a) * (pb - pa)
    (a, pa), (b, pb) = (knots[0], knots[1]) if t < knots[0][0] else (knots[-2], knots[-1])
    return pa + (t - a) / (b - a) * (pb - pa)

XB = 200                                  # Broad Street
U_KNOTS = [(-8.12, 14), (-1.65, 146), (0, XB), (0.91, XB + 125), (1.76, XB + 218), (2.10, XB + 224), (5.96, 456)]
V_KNOTS = [(-5.29, 1285), (-1.83, 1100), (-0.94, 1058), (-0.41, 1020), (0, 890), (1.65, 652), (2.19, 590), (10.17, 128)]
def P(lat, lon):
    u, v = rot(lat, lon)
    return interp(U_KNOTS, u), interp(V_KNOTS, v)

st = {}                                   # name -> (x, y)
for k, v in L.items():
    for s in v: st.setdefault(s["name"], P(s["lat"], s["lon"]))
for n in list(st):
    if abs(rot(*next((s["lat"], s["lon"]) for v in L.values() for s in v if s["name"] == n))[0]) < 0.1:
        st[n] = (XB, st[n][1])                                  # Broad Street stations exactly on the line
YM = st["15th St/City Hall"][1]                                 # Market Street
# West Market Street: evenly spaced in true order (69th St at the left edge).
WEST = ["69th St Transit Center", "Millbourne", "63rd St", "60th St", "56th St", "52nd St", "46th St", "40th St", "34th St",
        "Drexel Station at 30th St"]
for i, n in enumerate(WEST): st[n] = (20 + i * 14, YM)
# East Market Street, in true order.
for n, x in [("13th St", XB + 28), ("8th-Market", XB + 125), ("5th St/Independence Hall", XB + 172), ("2nd St", XB + 218)]:
    st[n] = (x, YM)
X8 = st["8th-Market"][0]
st["Chinatown"] = (X8, st["Chinatown"][1])
# 11th St MFL: the station exists but is not in SEPTA's current schedule (not served).
st["11th St"] = (XB + 64, YM)
# Trolley tunnel stops (one row under Market) and its two portals to the southwest.
YT = YM + 22
for n, x in [("13th St", XB + 28), ("15th St/City Hall", XB), ("19th St", XB - 18), ("22nd St", XB - 36),
             ("Drexel Station at 30th St", st["Drexel Station at 30th St"][0]), ("33rd St", st["Drexel Station at 30th St"][0] - 14)]:
    st["T " + n] = (x, YT)
x33 = st["T 33rd St"][0]
st["T 36th St Portal"] = (x33 - 22, YT + 24)
st["T 36th-Sansom"] = (x33 - 6, YT + 46)
st["T 37th-Spruce"] = (x33 - 18, YT + 70)
st["T 40th St Portal"] = (x33 - 34, YT + 96)
# PATCO (not SEPTA): Locust Street row, up 8th Street, Franklin Square. Positions in true order.
YL = 985
pat = {"15-16th & Locust": (XB - 12, YL), "12-13th & Locust": (XB + 40, YL), "9-10th & Locust": (XB + 92, YL)}
XP = X8 + 16
pat["Franklin Square"] = (XP + 30, YM - 38)

img = Image.new("1", (PW, PH), 1)
d = ImageDraw.Draw(img)
lines_mask = Image.new("1", (PW, PH), 0)    # where lines/markers are, for the overlap check
dm = ImageDraw.Draw(lines_mask)
labels = []                                  # (name, box)

def line(pts, w, fill=0, mask=True):
    d.line(pts, fill=fill, width=w, joint="curve")
    if mask and fill == 0: dm.line(pts, fill=1, width=w)
def dashed(pts, w, on=7, off=5):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = math.hypot(x1 - x0, y1 - y0); t = 0
        while t < n:
            t1 = min(t + on, n)
            line([(x0 + (x1 - x0) * t / n, y0 + (y1 - y0) * t / n), (x0 + (x1 - x0) * t1 / n, y0 + (y1 - y0) * t1 / n)], w)
            t = t1 + off
def mfl(pts):        # Market-Frankford: open (cased) line
    line(pts, 10); line(pts, 4, fill=1, mask=False)
def bsl(pts, w=8): line(pts, w)

def text(x, y, s, f=F_ST, anchor="la", rot=0, name=None, check=True, fill=0):
    """Draw text; rot=90 reads bottom-to-top, anchored at its bottom-centre ("above") or top-centre ("below")."""
    if rot == 0:
        d.text((x, y), s, font=f, anchor=anchor, fill=fill)
        box = d.textbbox((x, y), s, font=f, anchor=anchor)
    else:
        w = int(f.getlength(s)) + 4; h = f.size + 6
        t = Image.new("1", (w, h), 1); ImageDraw.Draw(t).text((2, 2), s, font=f, fill=0)
        t = t.rotate(rot, expand=True)
        ink = Image.eval(t.convert("L"), lambda p: 255 - p)
        bx0, by0, bx1, by1 = ink.getbbox()
        cx = (bx0 + bx1) / 2
        px = int(round(x - cx)); py = int(y - by0) if anchor == "below" else int(y - by1)
        img.paste(0, (px, py), ink.convert("1"))
        box = (px + bx0, py + by0, px + bx1, py + by1)
    if check: labels.append((name or s, box))
    return box

def multiline(x, y, rows, f=F_ST, anchor="la", gap=0, name=None):
    return [text(x, y + i * (f.size + gap), r, f, anchor, name=name) for i, r in enumerate(rows)]

# ---------- lines ----------
B1 = [st[s["name"]] for s in L["B1"]]
bsl(B1[1:])                                        # Olney .. NRG straight down Broad
bsl([B1[1], (B1[1][0] + 8, B1[1][1] - 10), B1[0]])  # the jog east to Fern Rock
xf, yf = st["Fairmount"]
bsl([(xf, yf), (X8, yf), st["Chinatown"], st["8th-Market"]], 6)
L1n = [s["name"] for s in L["L1"]]
mfl([st[n] for n in L1n])
# Trolley tunnel
tr = [st["T 13th St"], st["T 15th St/City Hall"], st["T 19th St"], st["T 22nd St"], st["T Drexel Station at 30th St"], st["T 33rd St"]]
line(tr, 4)
line([st["T 33rd St"], st["T 36th St Portal"]], 4)
line([st["T 33rd St"], st["T 36th-Sansom"], st["T 37th-Spruce"], st["T 40th St Portal"]], 4)
for p in (st["T 36th St Portal"], st["T 40th St Portal"]):     # portal: trolleys continue on the street
    dashed([p, (p[0], p[1] + 16)], 2, 3, 3)
# PATCO
dashed([pat["15-16th & Locust"], pat["12-13th & Locust"], pat["9-10th & Locust"], (XP, YL), (XP, YM + 10)], 3)
dashed([(XP, YM - 10), (XP, pat["Franklin Square"][1]), pat["Franklin Square"], (pat["Franklin Square"][0] + 34, pat["Franklin Square"][1])], 3)

# ---------- station markers ----------
EXPRESS = {s["name"] for s in L["B2"]}
def dot(p, r=3):           # local stop: a small white dot in the black line
    x, y = p; d.ellipse([x - r, y - r, x + r, y + r], fill=1); dm.ellipse([x - r - 2, y - r - 2, x + r + 2, y + r + 2], fill=1)
def ring(p, r=7, w=3):     # express / terminal / transfer: white circle, black ring
    x, y = p; d.ellipse([x - r, y - r, x + r, y + r], fill=0); dm.ellipse([x - r, y - r, x + r, y + r], fill=1)
    d.ellipse([x - r + w, y - r + w, x + r - w, y + r - w], fill=1)
def tick(p, r=3):
    x, y = p; d.ellipse([x - r, y - r, x + r, y + r], fill=1, outline=0, width=2); dm.ellipse([x - r, y - r, x + r, y + r], fill=1)
def capsule(a, b, r=8):    # transfer between two stations (rounded bar)
    line([a, b], 2 * r + 4); line([a, b], 2 * r - 2, fill=1, mask=False)

for s in L["B1"]:
    n = s["name"]
    (ring if n in EXPRESS else dot)(st[n])
ring(st["Fairmount"]); ring(st["Chinatown"])
for n in L1n:
    if n in ("15th St/City Hall", "8th-Market"): continue
    ring(st[n], 6, 2)
x11 = st["11th St"]; d.line([x11[0] - 5, YM - 5, x11[0] + 5, YM + 5], fill=0, width=3); d.line([x11[0] - 5, YM + 5, x11[0] + 5, YM - 5], fill=0, width=3)
capsule(st["15th St/City Hall"], st["T 15th St/City Hall"])
capsule(st["8th-Market"], (XP, YM))
capsule(st["Drexel Station at 30th St"], st["T Drexel Station at 30th St"])
for n in ["T 13th St", "T 19th St", "T 22nd St", "T 33rd St", "T 36th St Portal", "T 36th-Sansom", "T 37th-Spruce", "T 40th St Portal"]:
    tick(st[n])
capsule(st["13th St"], st["T 13th St"], 6)
for n in ["15-16th & Locust", "12-13th & Locust", "9-10th & Locust", "Franklin Square"]: tick(pat[n], 4)
d.line([st["Walnut-Locust"], pat["15-16th & Locust"]], fill=0, width=2)   # concourse to PATCO

# ---------- labels ----------
G = 13   # gap from line to label
def left(n, s=None, dy=0, f=F_ST, dx=0):
    x, y = st[n]; return text(x - G - dx, y + dy, s or n, f, "rm", name=n)
def right(n, s=None, dy=0, f=F_ST, dx=0):
    x, y = st[n]; return text(x + G + dx, y + dy, s or n, f, "lm", name=n)

# Broad Street, north: names on the left
for n, s in [("Olney Transit Center", "Olney T.C."), ("Logan", None), ("Wyoming", None), ("Hunting Park", None),
             ("Erie", None), ("Broad-Allegheny", "Allegheny"), ("North Philadelphia", None), ("Susquehanna-Dauphin", None),
             ("Cecil B. Moore", None), ("Broad-Girard", "Girard"), ("Fairmount", None)]:
    left(n, s)
text(st["Fern Rock Transit Center"][0] + 12, st["Fern Rock Transit Center"][1], "Fern Rock T.C.", F_ST, "lm", name="Fern Rock")
right("Broad-Spring Garden", "Spring Garden")
right("Race-Vine")
multiline(XB + G, YM - 40, ["City Hall", "15th St"], F_ST, "lm", name="City Hall")
text((XB + X8) / 2, st["Fairmount"][1] - 9, "B3 Broad-Ridge Spur", F_XS, "mb", name="spur label")
right("Chinatown", dy=0, dx=-2)
# Broad Street, south: names on the right
for n, s in [("Walnut-Locust", None), ("Lombard-South", None), ("Ellsworth-Federal", None), ("Tasker-Morris", None),
             ("Snyder", None), ("Oregon", None), ("NRG Station", "NRG (stadiums)")]:
    right(n, s)
# Market Street east: names below the line, reading upward
for n, s, dx in [("13th St", "13th St", 0), ("11th St", "11th St", 0), ("8th-Market", "8th-Market", -2),
                 ("5th St/Independence Hall", "5th St / Indep. Hall", 0), ("2nd St", "2nd St", 0)]:
    x, y = st[n]
    text(x + dx, (YT + 9) if n == "13th St" else (y + 12), s, F_ST, "below", rot=90, name=n)
# PATCO station names above the Locust Street row
text(pat["15-16th & Locust"][0] - 8, YL + 1, "15-16th", F_XS, "rm", name="15-16th")
text(pat["12-13th & Locust"][0], YL - 7, "12-13th", F_XS, "mb", name="12-13th")
text(pat["9-10th & Locust"][0], YL - 7, "9-10th", F_XS, "mb", name="9-10th")
text((pat["12-13th & Locust"][0] + pat["9-10th & Locust"][0]) / 2, YL + 6, "PATCO", F_XS, "mt", name="PATCO")
text(XP + 10, pat["Franklin Square"][1] + 7, "Franklin Sq", F_XS, "lt", name="Franklin")
# Frankford leg
left("Spring Garden", "Spring", dy=-8, f=F_ST); left("Spring Garden", "Garden", dy=8, f=F_ST)
for n, s in [("Front-Girard", "Girard"), ("Berks", None), ("York-Dauphin", None), ("Huntingdon", None), ("Somerset", None),
             ("Kensington-Allegheny", "Allegheny"), ("Tioga", None), ("Erie-Torresdale", None), ("Church", None),
             ("Arrott Transit Center", "Arrott T.C."), ("Frankford Transit Center", "Frankford T.C.")]:
    left(n, s)
# West Market Street: names above the line, reading upward
for n, s in [("Drexel Station at 30th St", "30th St"), ("34th St", None), ("40th St", None), ("46th St", None), ("52nd St", None),
             ("56th St", None), ("60th St", None), ("63rd St", None), ("Millbourne", None), ("69th St Transit Center", "69th St T.C.")]:
    x, y = st[n]; text(x, y - 11, s or n, F_SM, "above", rot=90, name=n)
# Trolley tunnel
for n, s in [("T 19th St", "19th"), ("T 22nd St", "22nd")]:
    x, y = st[n]; text(x, y + 9, s, F_SM, "below", rot=90, name=n)
text(st["T 33rd St"][0] - 9, YT - 1, "33rd", F_XS, "rm", name="33rd")
text(st["T 36th St Portal"][0] - 9, st["T 36th St Portal"][1] - 2, "T1 36th St portal", F_XS, "rm", name="36 portal")
text(st["T 36th-Sansom"][0] - 9, st["T 36th-Sansom"][1], "36th-Sansom", F_XS, "rm", name="36th-Sansom")
text(st["T 37th-Spruce"][0] - 9, st["T 37th-Spruce"][1], "37th-Spruce", F_XS, "rm", name="37th-Spruce")
multiline(st["T 40th St Portal"][0] - 9, st["T 40th St Portal"][1] - 2, ["40th St portal", "T2 T3 T4 T5"], F_XS, "rm", name="40 portal")

# ---------- header and legend ----------
d.text((14, 6), "SEPTA SUBWAY", font=F_TITLE, fill=0)
d.text((15, 46), "Broad Street, Market-Frankford", font=F_SUB, fill=0)
d.text((15, 64), "and the trolley tunnel", font=F_SUB, fill=0)
img_clock = CLOCK_BOX

lx, ly = 28, 1084
def legend_row(y, draw, label):
    draw(lx, y); text(lx + 46, y, label, F_SM, "lm", name="legend " + label)
KEY_BOX = (lx - 14, ly - 28, lx + 158, ly + 230)
d.rectangle(KEY_BOX, outline=0, width=2)
legend_row(ly + 4, lambda x, y: line([(x, y), (x + 34, y)], 8, mask=False), "B  Broad Street")
legend_row(ly + 22, lambda x, y: (line([(x, y), (x + 34, y)], 10, mask=False), line([(x, y), (x + 34, y)], 4, 1, False)), "L  Market-Frankford")
legend_row(ly + 44, lambda x, y: line([(x, y), (x + 34, y)], 4, mask=False), "T  Trolley tunnel")
legend_row(ly + 66, lambda x, y: [line([(x + i, y), (x + i + 7, y)], 3, mask=False) for i in (0, 12, 24)], "PATCO (to NJ)")
def leg_ring(x, y): line([(x, y), (x + 34, y)], 8, mask=False); d.ellipse([x + 10, y - 7, x + 24, y + 7], fill=0); d.ellipse([x + 13, y - 4, x + 21, y + 4], fill=1)
def leg_dot(x, y): line([(x, y), (x + 34, y)], 8, mask=False); d.ellipse([x + 14, y - 3, x + 20, y + 3], fill=1)
legend_row(ly + 94, leg_ring, "Express stop (B2)")
legend_row(ly + 116, leg_dot, "Local only (B1)")
def leg_x(x, y): line([(x, y), (x + 34, y)], 10, mask=False); line([(x, y), (x + 34, y)], 4, 1, False); d.line([x + 12, y - 5, x + 22, y + 5], fill=0, width=3); d.line([x + 12, y + 5, x + 22, y - 5], fill=0, width=3)
legend_row(ly + 138, leg_x, "Not served now")
for i, row in enumerate(["B3 Spur trains: Olney, Erie,", "N. Phila, Girard, Fairmount,", "Chinatown, 8th-Market.",
                         "Trolleys run on the street", "past the portals."]):
    text(lx - 4, ly + 155 + i * 14, row, F_XS, "la", name=f"legend note {i}")

# Connections (verified in the GTFS data: other SEPTA lines stopping within 200 m, rail stations within 350 m).
cx, cy = 216, 172
CONN_BOX = (cx - 4, cy - 26, cx + 100, cy + 340)
text(cx, cy - 20, "CONNECTIONS", F_SM, "la", name="legend conn")
conn = [("Fern Rock", ["Regional Rail"]), ("North Phila.", ["Amtrak, Regional", "Rail (North Broad)"]),
        ("City Hall", ["Regional Rail", "(Suburban Sta.)"]), ("13th St", ["Regional Rail", "(Jefferson Sta.)"]),
        ("30th St", ["Amtrak, Regional", "Rail (Gray 30th St)"]), ("PATCO", ["Walnut-Locust (15-16th),", "8th-Market"]),
        ("69th St T.C.", ["M1 Norristown,", "D1 D2 trolleys"]), ("Girard (both)", ["G1 trolley"])]
y = cy
for head, rows in conn:
    text(cx, y, head, F_SM, "la", name="legend " + head); y += 15
    for r in rows: text(cx + 6, y, r, F_XS, "la", name="legend " + head + r); y += 14
    y += 5
text(lx - 4, ly - 19, "KEY", F_SM, "la", name="legend key")

d.line([(14, FOOTER_Y - 4), (PW - 14, FOOTER_Y - 4)], fill=0, width=2)

# ---------- overlap check ----------
def overlap(a, b): return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
problems = []
for i, (na, a) in enumerate(labels):
    if a[0] < 0 or a[1] < 0 or a[2] > PW or a[3] > PH: problems.append(f"off canvas: {na} {a}")
    for nb, b in labels[i + 1:]:
        if overlap(a, b): problems.append(f"labels overlap: {na} / {nb}")
    crop = lines_mask.crop(tuple(int(v) for v in a))
    if crop.getbbox(): problems.append(f"label on a line: {na}")
    if overlap(a, CLOCK_BOX) or a[3] > FOOTER_Y - 6: problems.append(f"in a reserved box: {na}")
    if overlap(a, KEY_BOX) and not na.startswith("legend"): problems.append(f"under the key: {na}")
if lines_mask.crop(KEY_BOX).getbbox(): problems.append("the key covers a line")
if lines_mask.crop(CONN_BOX).getbbox(): problems.append("connections box covers a line")
for na, a in labels:
    if overlap(a, CONN_BOX) and not na.startswith("legend"): problems.append(f"under connections: {na}")
print("\n".join(problems) if problems else "no overlaps")

# ---------- outputs ----------
ap = argparse.ArgumentParser(); ap.add_argument("--preview"); args = ap.parse_args()
if args.preview: img.convert("L").save(args.preview)

# 1-bit rows, 1 = black, MSB first, 60 bytes per row; run-length encoded as (count, byte) pairs over the byte stream.
raw = bytearray()
px = img.load()
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
out = ROOT / "firmware/Display/subway_map.h"
rows = ",".join(str(b) for b in enc)
out.write_text(f"""// Generated by tools/subway_map/make_map.py from {data['source']}. Do not edit by hand.
// Portrait map, {PW} x {PH}, 1 = black, MSB = leftmost pixel, {PW // 8} bytes per row, run-length encoded as
// (count, value) byte pairs. {len(raw):,} bytes unpacked, {len(enc):,} packed.
#pragma once
#include <stdint.h>
namespace septa_map {{
constexpr int PW = {PW}, PH = {PH};
constexpr int CLOCK_X0 = {CLOCK_BOX[0]}, CLOCK_Y0 = {CLOCK_BOX[1]}, CLOCK_X1 = {CLOCK_BOX[2]}, CLOCK_Y1 = {CLOCK_BOX[3]};
constexpr int FOOTER_Y = {FOOTER_Y};
constexpr uint32_t RLE_BYTES = {len(enc)};
static const uint8_t RLE[{len(enc)}] PROGMEM = {{{rows}}};
}}
""")
print(f"wrote {out.relative_to(ROOT)} ({len(enc):,} bytes packed)")
