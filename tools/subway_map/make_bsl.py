"""Broad Street Line map for the portrait e-paper screen, after SEPTA's own BSL line map (Version 2.0,
schedules.septa.org/maps/line-map-bsl.pdf), redrawn in black and white for 480 x 1360.

    python -I tools/subway_map/make_bsl.py [--preview out.png]

Writes firmware/Display/subway_bsl.h. Station data below (bus routes, which are frequent, accessibility,
reverse-direction stations, notes) is transcribed from that map; stop patterns match SEPTA's GTFS
(septa_subway.json). Colour mapping: orange/local station -> white circle with black ring; black/transfer
station -> black; frequent (red) bus route -> outlined pill; standard (black) bus route -> black pill.
"""
import argparse, json, math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).parent
ROOT = HERE.parent.parent
PW, PH = 480, 1360
CX = Path("/usr/share/fonts/truetype/crosextra")
def font(sz, bold=True): return ImageFont.truetype(str(CX / ("Carlito-Bold.ttf" if bold else "Carlito-Regular.ttf")), sz)
F_NAME_B, F_NAME = font(22), font(22, False)
F_PILL, F_SMALL, F_TAG, F_NOTE = font(13), font(12), font(12), font(12)
F_HEAD, F_BADGE, F_DIR = font(31), font(24), font(28)
F_ICON = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 17)
HEADER_H = 64
CLOCK_BOX = (300, 2, 474, 62)          # firmware draws the time here, white on the black header
FOOTER_Y = 1318

data = json.loads((HERE / "septa_subway.json").read_text())
B1 = [s["name"] for s in data["lines"]["B1"]]
assert B1[0].startswith("Fern Rock") and B1[-1] == "NRG Station" and len(B1) == 22

# name on the map, bold?, accessible?, reverse-direction arrows?, bus rows (F = frequent), extras
F = "F"
S = {
 "Fern Rock Transit Center": dict(n=["Fern Rock", "Transit Center"], b=1, a=1, r=1, bus=[[(57, F), (70, F), 4, 28]], rr=["Fern Rock", "Transit Center"]),
 "Olney Transit Center": dict(n=["Olney", "Transit Center"], b=1, a=1, r=1, bus=[[(6, F), (18, F), (51, F)], [16, 22, 26, 55]],
                              note=("B3", ["Limited weekday service to", "Fern Rock Transit Center"])),
 "Logan": dict(n=["Logan"], bus=[[16, 41]]),
 "Wyoming": dict(n=["Wyoming"], bus=[[16, 75]]),
 "Hunting Park": dict(n=["Hunting Park"], bus=[[(82, F), 1, 16]]),
 "Erie": dict(n=["Erie"], b=1, r=1, bus=[[(23, F), (56, F), 16], [53, 71, 81]]),
 "Broad-Allegheny": dict(n=["Broad-Allegheny"], a=1, bus=[[(60, F), 4, 16]]),
 "North Philadelphia": dict(n=["North Philadelphia"], b=1, a=1, r=1, bus=[[4, 16, 54]], rr=["North Broad Sta.", "North Philadelphia Sta."]),
 "Susquehanna-Dauphin": dict(n=["Susquehanna-Dauphin"], a=1, bus=[[4, 16, 39]]),
 "Cecil B. Moore": dict(n=["Cecil B. Moore"], a=1, bus=[[(3, F), 4, 16]]),
 "Broad-Girard": dict(n=["Broad-Girard"], b=1, a=1, r=1, bus=[[4, 16]], tags=["G"]),
 "Fairmount": dict(n=["Fairmount"], b=1, r=1, bus=[[4, 16, 61]]),
 "Broad-Spring Garden": dict(n=["Broad-", "Spring", "Garden"], b=1, a=1, r=1, bus=[[4, 16, 43]]),
 "Race-Vine": dict(n=["Race-Vine"], b=1, a=1, r=1, bus=[[4, 16], [("Southbound", None), 27]]),
 "15th St/City Hall": dict(n=["15th St/", "City Hall"], b=1, r=1, bus=[[(17, F), (33, F), (48, F), 2], [4, 16, 27], [31, 32, 38], [44, 124, 125]],
                           tags=["L", "T"], rr=["Suburban Station"]),
 "Walnut-Locust": dict(n=["Walnut-Locust"], b=1, a=1, r=1, bus=[[(21, F), 4, 9, 12], [27, 32, 42]], tags=["PATCO"],
                       note=("B2", ["Express Service to NRG", "Station (Special Events Only)"])),
 "Lombard-South": dict(n=["Lombard-South"], r=1, bus=[[4, 27, 32, 40]]),
 "Ellsworth-Federal": dict(n=["Ellsworth-Federal"], r=1, bus=[[(64, F), 4]]),
 "Tasker-Morris": dict(n=["Tasker-Morris"], a=1, r=1, bus=[[4, 29]]),
 "Snyder": dict(n=["Snyder"], r=1, bus=[[(79, F), 4, 37]]),
 "Oregon": dict(n=["Oregon"], a=1, r=1, bus=[[(63, F), 4, 7], [45, 68]]),
 "NRG Station": dict(n=["NRG Station"], b=1, a=1, r=1, bus=[[4, 45], [("Limited Service", None), 68]], tags=["nrg"],
                     note=("B2", ["Express Service to NRG", "Station (Special Events Only)"])),
}
SPUR = {
 "Chinatown": dict(n=["Chinatown"], r=1, bus=[[(47, F)], [61, ("Southbound", None)]]),
 "8th-Market": dict(n=["8th-Market"], b=1, a=1, r=1, bus=[[(17, F), (33, F), (47, F)], [(48, F), 38, 44], [61]], tags=["L", "PATCO"]),
}
B2_STOPS = {s["name"] for s in data["lines"]["B2"]}
B3_STOPS = {s["name"] for s in data["lines"]["B3"]}
assert {"Erie", "North Philadelphia", "Broad-Girard", "Fairmount"} <= B3_STOPS and "North Philadelphia" not in B2_STOPS

img = Image.new("1", (PW, PH), 1)
d = ImageDraw.Draw(img)
mask = Image.new("1", (PW, PH), 0); dm = ImageDraw.Draw(mask)
boxes = []
def reg(name, box): boxes.append((name, tuple(int(v) for v in box)))
def text(x, y, s, f, anchor="la", fill=0, name=None, **kw):
    d.text((x, y), s, font=f, anchor=anchor, fill=fill, **kw)
    b = d.textbbox((x, y), s, font=f, anchor=anchor, **kw)
    if name: reg(name, b)
    return b
def line(pts, w, fill=0):
    d.line(pts, fill=fill, width=w)
    if fill == 0: dm.line(pts, fill=1, width=w)

# ---------- geometry ----------
X1, X2, X3 = 172, 196, 220        # B1, B2, B3 strands
SW = 10                           # strand width
XS = 352                          # the spur's vertical, Chinatown and 8th-Market
PILL_R = 13                       # station marker radius
XN_N, XN_S = 246, 222             # names: north of Fairmount (3 strands), south (2 strands)
XB = 150                          # bus pills end here (right-aligned)
PILL_H, PILL_ROW = 17, 19

def bus_w(item):
    if isinstance(item, tuple) and item[1] is None: return F_SMALL.getlength(item[0]) + 3
    n = item[0] if isinstance(item, tuple) else item
    return max(22, F_PILL.getlength(str(n)) + 11)
def bus_rows_h(rows): return len(rows) * PILL_ROW

def right_lines(info):
    """Lines of the right-hand block: (kind, payload, height)."""
    out = [("name", (s, info.get("b")), 24) for s in info["n"]]
    if info.get("rr") or info.get("tags"): out.append(("tags", info, 20 + (12 if info.get("rr") else 0)))
    if info.get("note"): out.append(("note", info["note"], 30))
    return out

# Row heights: each station's block (names, tags, notes on the right; bus pills on the left) centred on its row.
order = B1
ext = []
for n in order:
    info = S[n]
    rh = sum(h for _, _, h in right_lines(info))
    top = -12                                     # first name line is centred on the row
    bot = top + rh
    pb = bus_rows_h(info["bus"]) / 2
    top, bot = min(top, -pb), max(bot, pb, PILL_R)
    if n == "Broad-Spring Garden": top = min(top, -34)
    if n == "Race-Vine":       bot = max(bot, -12 + sum(h for *_, h in right_lines(SPUR["Chinatown"])) + bus_rows_h(SPUR["Chinatown"]["bus"]))
    if n == "15th St/City Hall":
        top = min(top, -14)
        bot = max(bot, 8 + sum(h for *_, h in right_lines(S[n])), 14 + bus_rows_h(SPUR["8th-Market"]["bus"]) + 22)
    ext.append((top, bot))
Y_FIRST, Y_LAST = 124, 1200
need = [ext[i][1] - ext[i + 1][0] + 3 for i in range(len(order) - 1)]
extra = (Y_LAST - Y_FIRST - sum(need)) / len(need)
assert extra >= 0, f"does not fit by {-extra * len(need):.0f}px"
row, y = {}, Y_FIRST
for n, g in zip(order, need + [0]):
    row[n] = round(y); y += g + extra
yF = row["Fairmount"]
row["Chinatown"], row["8th-Market"] = row["Race-Vine"], row["15th St/City Hall"]

# ---------- header ----------
d.rectangle([0, 0, PW, HEADER_H], fill=0)
d.rectangle([12, 8, 60, 56], outline=1, width=3)
d.text((36, 33), "B", font=F_BADGE, anchor="mm", fill=1)
d.text((72, 32), "Broad Street Line", font=F_HEAD, anchor="lm", fill=1)

# ---------- strands ----------
def dashed_v(x, y0, y1, w, on=9, off=6):
    y = y0
    while y < y1:
        line([(x, y), (x, min(y + on, y1))], w); y += on + off
yTop = row[order[0]]
for x, lbl in ((X1, "B1"), (X2, "B2"), (X3, "B3")):            # badges above Fern Rock
    d.rectangle([x - 12, yTop - 50, x + 11, yTop - 26], fill=0)
    d.text((x, yTop - 38), lbl, font=F_PILL, anchor="mm", fill=1)
    reg("badge " + lbl, (x - 12, yTop - 50, x + 11, yTop - 26))
line([(X1, yTop - 26), (X1, row["NRG Station"])], SW)
line([(X2, yTop - 26), (X2, row["Walnut-Locust"])], SW)
dashed_v(X2, row["Walnut-Locust"] + PILL_R, row["NRG Station"], SW)          # B2 to NRG: special events only
# B3: Fern Rock - Olney is limited weekday service (hollow), then solid to Fairmount and the spur.
line([(X3, yTop - 26), (X3, row["Olney Transit Center"])], SW); line([(X3, yTop - 26), (X3, row["Olney Transit Center"])], SW - 6, fill=1)
yK = (yF + PILL_R + row["Broad-Spring Garden"] - 12) // 2         # where the spur turns east, between the two stations
line([(X3, row["Olney Transit Center"]), (X3, yK)], SW)
line([(X3, yK), (XS, yK)], SW); line([(XS, yK), (XS, row["8th-Market"])], SW)
d.ellipse([X3 - SW // 2, yK - SW // 2, X3 + SW // 2, yK + SW // 2], fill=0); d.ellipse([XS - SW // 2, yK - SW // 2, XS + SW // 2, yK + SW // 2], fill=0)
# Strand labels just south of Erie and Fairmount, as on SEPTA's map.
for yy, labs in ((yK, ((X1, "B1"), (X2, "B2"))),):
    for x, l in labs:
        d.rectangle([x - 8, yy - 7, x + 8, yy + 7], fill=0); d.text((x, yy), l, font=F_SMALL, anchor="mm", fill=1)
d.rectangle([(X3 + XS) / 2 - 10, yK - 7, (X3 + XS) / 2 + 10, yK + 7], fill=0); d.text(((X3 + XS) / 2, yK), "B3", font=F_SMALL, anchor="mm", fill=1)
# L (Market-Frankford) between 15th St and 8th-Market: an open (cased) line.
yC = row["15th St/City Hall"]
line([(X2 + 12, yC), (XS - 12, yC)], 10); line([(X2 + 12, yC), (XS - 12, yC)], 4, fill=1)
# Bottom badges
yN = row["NRG Station"]
for x, lbl in ((X1, "B1"), (X2, "B2")):
    d.rectangle([x - 12, yN + 20, x + 11, yN + 44], fill=0); d.text((x, yN + 32), lbl, font=F_PILL, anchor="mm", fill=1)
line([(X1, yN), (X1, yN + 20)], SW); line([(X2, yN), (X2, yN + 20)], SW)
# B3 badge at the end of the spur
y8 = row["8th-Market"]
d.rectangle([XS - 15, y8 + 18, XS + 15, y8 + 46], fill=0); d.text((XS, y8 + 32), "B3", font=F_PILL, anchor="mm", fill=1)
reg("B3 end", (XS - 15, y8 + 18, XS + 15, y8 + 46))

# ---------- station markers ----------
def wheelchair(x, y, white):
    d.text((x, y + 1), "♿", font=F_ICON, anchor="mm", fill=1 if white else 0)
def circle(x, y, black, acc):
    d.ellipse([x - PILL_R, y - PILL_R, x + PILL_R, y + PILL_R], fill=0)
    if not black: d.ellipse([x - PILL_R + 4, y - PILL_R + 4, x + PILL_R - 4, y + PILL_R - 4], fill=1)
    if acc: wheelchair(x, y, black)
    dm.ellipse([x - PILL_R, y - PILL_R, x + PILL_R, y + PILL_R], fill=1)
def pill(x0, x1, y, acc):
    d.rounded_rectangle([x0 - PILL_R, y - PILL_R, x1 + PILL_R, y + PILL_R], radius=PILL_R, fill=0)
    dm.rectangle([x0 - PILL_R, y - PILL_R, x1 + PILL_R, y + PILL_R], fill=1)
    if acc: wheelchair((x0 + x1) / 2, y, True)
def arrows(x0, x1, y):
    """SEPTA's 'reverse direction without additional fare' marks: an arc on each side of the station."""
    r = PILL_R + 5
    d.arc([x0 - r, y - r, x0 + r, y + r], 120, 240, fill=0, width=2)
    d.arc([x1 - r, y - r, x1 + r, y + r], 300, 60, fill=0, width=2)
    for (ax, ay, dx) in ((x0 - r * math.cos(math.radians(60)), y + r * math.sin(math.radians(60)), 1), (x1 + r * math.cos(math.radians(60)), y - r * math.sin(math.radians(60)), -1)):
        d.polygon([(ax - 3, ay - 3 * dx), (ax + 3, ay - 3 * dx), (ax + 4 * dx, ay + 2 * dx)], fill=0)
    dm.rectangle([x0 - r - 2, y - r, x1 + r + 2, y + r], fill=1)

for n in order:
    y, info = row[n], S[n]
    stops = [x for x, s in ((X1, True), (X2, n in B2_STOPS), (X3, n in B3_STOPS)) if s]
    if n == "Olney Transit Center": stops = [X1, X2, X3]
    if n == "Fern Rock Transit Center": stops = [X1, X2, X3]
    if n == "NRG Station": stops = [X1, X2]
    acc = bool(info.get("a"))
    if len(stops) == 1:
        circle(X1, y, False, acc)
        if info.get("r"): arrows(X1, X1, y)
    elif stops == [X1, X3]:                  # North Philadelphia, Fairmount: B1 and B3 platforms, linked
        line([(X1, y), (X3, y)], 6)
        circle(X1, y, True, acc); circle(X3, y, True, acc)
        if n == "North Philadelphia": arrows(X1, X1, y); arrows(X3, X3, y)
        else: arrows(X3, X3, y)
    else:
        pill(stops[0], stops[-1], y, acc)
        if info.get("r"): arrows(stops[0], stops[-1], y)
circle(XS, row["Chinatown"], False, False); arrows(XS, XS, row["Chinatown"])
circle(XS, row["8th-Market"], True, True); arrows(XS, XS, row["8th-Market"])

# ---------- bus pills (left) ----------
def bus_block(rows, xr, yc, name, align="r"):
    y0 = yc - len(rows) * PILL_ROW / 2 + PILL_ROW / 2
    for i, r in enumerate(rows):
        yy = y0 + i * PILL_ROW
        widths = [bus_w(it) + 3 for it in r]
        x = xr - sum(widths) if align == "r" else xr
        for it, w in zip(r, widths):
            w -= 3
            if isinstance(it, tuple) and it[1] is None:
                text(x, yy, it[0], F_SMALL, "lm", name=name + " busnote"); x += w + 3; continue
            num, freq = (it if isinstance(it, tuple) else (it, None))
            box = [x, yy - PILL_H / 2, x + w, yy + PILL_H / 2]
            if freq: d.rounded_rectangle(box, radius=PILL_H // 2, fill=1, outline=0, width=2); tf = 0
            else: d.rounded_rectangle(box, radius=PILL_H // 2, fill=0); tf = 1
            d.text((x + w / 2, yy), str(num), font=F_PILL, anchor="mm", fill=tf)
            reg(name + " bus", box); x += w + 3
for n in order:
    left = XB - (8 if S[n].get("r") else 0)
    bus_block(S[n]["bus"], left, row[n], n)

# ---------- names, tags, notes (right) ----------
def tag_box(x, y, s, n):
    """Small black box with white text (L, T, G, Regional Rail); PATCO and nrg as plain bold words."""
    if s in ("PATCO", "nrg"):
        b = text(x, y, s, F_TAG if s == "PATCO" else font(15), "lm", name=n + " tag"); return b[2] + 6
    w = F_TAG.getlength(s) + 8
    d.rectangle([x, y - 8, x + w, y + 8], fill=0); d.text((x + w / 2, y), s, font=F_TAG, anchor="mm", fill=1)
    reg(n + " tag", (x, y - 8, x + w, y + 8)); return x + w + 4

def right_block(n, info, x, y, name_dy=0):
    yy = y - 12 + name_dy
    for kind, payload, h in right_lines(info):
        if kind == "name":
            s, bold = payload
            text(x, yy + 12, s, F_NAME_B if bold else F_NAME, "lm", name=n)
        elif kind == "tags":
            xx, ty = x, yy + 10
            for t in info.get("tags", []):
                xx = tag_box(xx, ty, t, n)
            if info.get("rr"):
                x_rr = xx
                xx = tag_box(xx, ty, "Regional Rail", n)
                text(x_rr, ty + 15, " / ".join(r.rstrip(",") for r in info["rr"]) if n != "Fern Rock Transit Center" else "Fern Rock Transit Center",
                     F_SMALL, "lm", name=n + " rr")
        elif kind == "note":
            badge, rows_ = payload
            d.rectangle([x, yy + 2, x + 20, yy + 18], fill=0); d.text((x + 10, yy + 10), badge, font=F_SMALL, anchor="mm", fill=1)
            w = max(F_NOTE.getlength(r) for r in rows_) + 8
            bx = [x + 24, yy, x + 24 + w, yy + 28]
            for x0 in range(int(bx[0]), int(bx[2]), 6):                      # dashed box
                d.line([(x0, bx[1]), (min(x0 + 3, bx[2]), bx[1])], fill=0); d.line([(x0, bx[3]), (min(x0 + 3, bx[2]), bx[3])], fill=0)
            for y0 in range(int(bx[1]), int(bx[3]), 6):
                d.line([(bx[0], y0), (bx[0], min(y0 + 3, bx[3]))], fill=0); d.line([(bx[2], y0), (bx[2], min(y0 + 3, bx[3]))], fill=0)
            for i, r in enumerate(rows_): d.text((bx[0] + 4, yy + 2 + i * 13), r, font=F_NOTE, fill=0)
            reg(n + " note", (x, yy, bx[2], bx[3]))
        yy += h

for n in order:
    info = S[n]
    north = order.index(n) <= order.index("Fairmount")
    x = XN_N if north else XN_S
    dy = 26 if n == "15th St/City Hall" else 0                   # below the L line, as on SEPTA's map
    right_block(n, info, x, row[n], dy)
for n in ("Chinatown", "8th-Market"):
    y, info = row[n], SPUR[n]
    right_block(n, info, XS + 24, y)
    nl = sum(h for *_, h in right_lines(info))
    bus_block(info["bus"], XS + 24, y - 12 + nl + bus_rows_h(info["bus"]) / 2, n, align="l")

# ---------- direction arrows (outlined letters, like SEPTA's grey) ----------
def direction(word, y_mid, up):
    tw = int(F_DIR.getlength(word))
    t = Image.new("1", (tw + 44, 40), 1)
    td = ImageDraw.Draw(t)
    if up:    # text then an arrow pointing right; rotated, the arrow is at the top pointing up
        td.text((4, 2), word, font=F_DIR, fill=1, stroke_width=2, stroke_fill=0)
        td.line([(tw + 12, 20), (tw + 36, 20)], fill=0, width=4); td.polygon([(tw + 42, 20), (tw + 30, 9), (tw + 30, 31)], fill=0)
    else:     # an arrow pointing left then text; rotated, the arrow is at the bottom pointing down
        td.text((40, 2), word, font=F_DIR, fill=1, stroke_width=2, stroke_fill=0)
        td.line([(8, 20), (32, 20)], fill=0, width=4); td.polygon([(2, 20), (14, 9), (14, 31)], fill=0)
    t = t.rotate(90, expand=True)
    x0, y0 = PW - 8 - t.width, int(y_mid - t.height / 2)
    img.paste(0, (x0, y0), Image.eval(t.convert("L"), lambda p: 255 - p).convert("1"))
    reg("dir " + word, (x0, y0, x0 + t.width, y0 + t.height))
direction("Northbound", (row["Logan"] + row["Erie"]) / 2, True)
direction("Southbound", (row["Ellsworth-Federal"] + row["Oregon"]) / 2, False)

# ---------- key (bottom) ----------
ky = FOOTER_Y - 44
d.line([(14, ky - 10), (PW - 14, ky - 10)], fill=0, width=1)
x = 14
d.rounded_rectangle([x, ky - 8, x + 24, ky + 8], radius=8, fill=1, outline=0, width=2); d.text((x + 12, ky), "21", font=F_PILL, anchor="mm", fill=0)
text(x + 28, ky, "Frequent bus", F_SMALL, "lm", name="key1")
x = 128
d.rounded_rectangle([x, ky - 8, x + 24, ky + 8], radius=8, fill=0); d.text((x + 12, ky), "4", font=F_PILL, anchor="mm", fill=1)
text(x + 28, ky, "Bus", F_SMALL, "lm", name="key2")
x = 196
d.ellipse([x, ky - 9, x + 18, ky + 9], fill=0); d.ellipse([x + 4, ky - 5, x + 14, ky + 5], fill=1)
text(x + 22, ky, "Local", F_SMALL, "lm", name="key3")
x = 268
d.rounded_rectangle([x, ky - 9, x + 30, ky + 9], radius=9, fill=0)
text(x + 34, ky, "Transfer", F_SMALL, "lm", name="key4")
x = 356
d.ellipse([x, ky - 9, x + 18, ky + 9], fill=0); wheelchair(x + 9, ky, True)
text(x + 22, ky, "Accessible", F_SMALL, "lm", name="key5")
ky2 = ky + 22
r = 9; cxk = 22
d.arc([cxk - r, ky2 - r, cxk + r, ky2 + r], 120, 240, fill=0, width=2); d.arc([cxk - r + 4, ky2 - r, cxk + r + 4, ky2 + r], 300, 60, fill=0, width=2)
text(40, ky2, "Reverse direction without additional fare.   Dashed: limited service.", F_SMALL, "lm", name="key6")

d.line([(14, FOOTER_Y - 4), (PW - 14, FOOTER_Y - 4)], fill=0, width=2)

# ---------- checks ----------
def overlap(a, b): return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
problems = []
for i, (na, a) in enumerate(boxes):
    if a[0] < 0 or a[2] > PW: problems.append(f"off canvas: {na} {a}")
    for nb, b in boxes[i + 1:]:
        if overlap(a, b) and na.split(" ")[0] != nb.split(" ")[0]: problems.append(f"overlap: {na} / {nb}")
    if not (na.startswith("badge") or na.startswith("B3 end")) and mask.crop(a).getbbox(): problems.append(f"on a line or marker: {na}")
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
out.write_text(f"""// Generated by tools/subway_map/make_bsl.py (after SEPTA's Broad Street Line map, Version 2.0; stop patterns from
// {data['source']}). Do not edit by hand.
// Portrait {PW} x {PH}, same format as subway_map.h. The clock goes in the black header, in white. {len(enc):,} bytes packed.
#pragma once
#include <stdint.h>
namespace septa_bsl {{
constexpr int CLOCK_X0 = {CLOCK_BOX[0]}, CLOCK_Y0 = {CLOCK_BOX[1]}, CLOCK_X1 = {CLOCK_BOX[2]}, CLOCK_Y1 = {CLOCK_BOX[3]};
constexpr uint32_t RLE_BYTES = {len(enc)};
static const uint8_t RLE[{len(enc)}] PROGMEM = {{{",".join(str(b) for b in enc)}}};
}}
""")
print(f"wrote {out.relative_to(ROOT)} ({len(enc):,} bytes packed)")
