"""Extract SEPTA's subway network from the official GTFS feed into septa_subway.json (committed).

    python -I tools/subway_map/extract.py path/to/google_bus_dir

The feed: https://github.com/septadev/GTFS/releases/latest/download/gtfs_public.zip
(unzip it, then unzip google_bus.zip inside). Lines: B1 local, B2 express, B3 Broad-Ridge Spur, L1 Market-Frankford,
and the trolley tunnel part of T1-T5. Also records which other lines stop next to each station.
"""
import collections, csv, json, math, sys
from pathlib import Path

d = Path(sys.argv[1])
rd = lambda name: csv.DictReader(open(d / name, encoding="utf-8-sig"))
stops = {r["stop_id"]: r for r in rd("stops.txt")}
feed = next(rd("feed_info.txt"))
ROUTES = ["B1", "B2", "B3", "L1", "T1", "T2", "T3", "T4", "T5", "G1", "D1", "D2", "M1"]
trips = {r["trip_id"]: r for r in rd("trips.txt") if r["route_id"] in ROUTES}
seq = collections.defaultdict(list)
for r in rd("stop_times.txt"):
    if r["trip_id"] in trips:
        seq[r["trip_id"]].append((int(r["stop_sequence"]), r["stop_id"]))

# Most common stop pattern (and its shape) per route, direction 0 (B/L: southbound/westbound... as SEPTA codes it).
pat = collections.defaultdict(collections.Counter)
shape_of = {}
for tid, s in seq.items():
    t = trips[tid]
    ids = tuple(x[1] for x in sorted(s))
    pat[(t["route_id"], t["direction_id"])][ids] += 1
    shape_of[(t["route_id"], t["direction_id"], ids)] = t["shape_id"]

def main_pattern(route, direction="0"):
    ids, _ = pat[(route, direction)].most_common(1)[0]
    return ids, shape_of[(route, direction, ids)]

def clean(name):
    for suf in (" - B2 & B3", " - B1", " - B2", " - B3"):
        name = name.replace(suf, "")
    return name

def stop(sid):
    s = stops[sid]
    return {"name": clean(s["stop_name"]), "lat": round(float(s["stop_lat"]), 5), "lon": round(float(s["stop_lon"]), 5)}

needed_shapes = {}
lines = {}
for route in ["B1", "B2", "B3", "L1"]:
    ids, shp = main_pattern(route)
    lines[route] = [stop(i) for i in ids]
    needed_shapes[route] = shp
# Trolley tunnel: T1 (via 36th St Portal) and T2 (via 40th St Portal, same as T3-T5), from 13th St to the portal.
for route, portal in [("T1", "36th St Portal"), ("T2", "40th St Portal")]:
    ids, shp = main_pattern(route, "1")   # eastbound...westbound pattern that starts at 13th St
    names = [stops[i]["stop_name"] for i in ids]
    end = names.index(portal)
    lines[route + "_tunnel"] = [stop(i) for i in ids[: end + 1]]
    needed_shapes[route + "_tunnel"] = shp

shapes = collections.defaultdict(list)
want = set(needed_shapes.values())
for r in rd("shapes.txt"):
    if r["shape_id"] in want:
        shapes[r["shape_id"]].append((int(r["shape_pt_sequence"]), float(r["shape_pt_lat"]), float(r["shape_pt_lon"])))

def km(a, b):
    return math.hypot((a[0] - b[0]) * 111.0, (a[1] - b[1]) * 85.2)

def clip(pts, first, last):
    """Part of a polyline between the points nearest two stations."""
    i = min(range(len(pts)), key=lambda k: km(pts[k], first))
    j = min(range(len(pts)), key=lambda k: km(pts[k], last))
    if i > j: i, j = j, i
    return pts[i : j + 1]

geom = {}
for key, shp in needed_shapes.items():
    pts = [(la, lo) for _, la, lo in sorted(shapes[shp])]
    st = lines[key]
    pts = clip(pts, (st[0]["lat"], st[0]["lon"]), (st[-1]["lat"], st[-1]["lon"]))
    # thin to ~25 m steps
    out = [pts[0]]
    for p in pts[1:]:
        if km(p, out[-1]) > 0.025: out.append(p)
    out.append(pts[-1])
    geom[key] = [[round(a, 5), round(b, 5)] for a, b in out]

# Other SEPTA lines stopping within 200 m of each station (for connection notes).
near = collections.defaultdict(set)
route_stops = collections.defaultdict(set)
for tid, s in seq.items():
    for _, sid in s: route_stops[trips[tid]["route_id"]].add(sid)
allst = {(s["name"], s["lat"], s["lon"]) for v in lines.values() for s in v}
for name, la, lo in allst:
    for r in ["G1", "D1", "D2", "M1", "T1", "T2", "T3", "T4", "T5"]:
        if any(km((la, lo), (float(stops[i]["stop_lat"]), float(stops[i]["stop_lon"]))) < 0.2 for i in route_stops[r]):
            near[name].add(r)

# Broad Street Line connections: every bus route with a stop within 150 m of the station (school-day 400-series
# routes left out), plus rail stations within 350 m from the rail feed (google_rail, next to google_bus) if present.
all_trips = {r["trip_id"]: r["route_id"] for r in rd("trips.txt")}
routes = {r["route_id"]: r["route_short_name"] for r in rd("routes.txt")}
stop_routes = collections.defaultdict(set)
for r in rd("stop_times.txt"):
    stop_routes[r["stop_id"]].add(all_trips[r["trip_id"]])
def is_bus(rid):
    if rid in ROUTES or rid.endswith("_OWL") or rid in ("L1", "L2"): return False
    return not (rid.isdigit() and 400 <= int(rid) < 500)
bsl_conn = {}
rail_dir = d.parent / "rail"
rail = list(csv.DictReader(open(rail_dir / "stops.txt", encoding="utf-8-sig"))) if (rail_dir / "stops.txt").exists() else []
for s in lines["B1"] + lines["B3"][-2:]:
    c = (s["lat"], s["lon"]); found = set()
    for sid, rr in stop_routes.items():
        stp = stops[sid]
        if km(c, (float(stp["stop_lat"]), float(stp["stop_lon"]))) < 0.15: found |= rr
    bus = sorted({routes[r] for r in found if is_bus(r)}, key=lambda x: (len(x), x))
    metro = sorted({r for r in found if r in ("L1", "T1", "T2", "T3", "T4", "T5", "G1")})
    rr = sorted({r["stop_name"] for r in rail if km(c, (float(r["stop_lat"]), float(r["stop_lon"]))) < 0.35})
    bsl_conn[s["name"]] = {"bus": bus, "metro": metro, "rail": rr, "owl": any(r == "B1_OWL" for r in found)}

out = {"source": f"SEPTA GTFS {feed['feed_version']} (valid {feed['feed_start_date']}-{feed['feed_end_date']})",
       "lines": lines, "shapes": geom, "nearby": {k: sorted(v) for k, v in sorted(near.items())}, "bsl_connections": bsl_conn,
       "patterns": {r: [[st["name"] for st in [stop(i) for i in ids]], n] for r in ("B1", "B2", "B3")
                    for ids, n in pat[(r, "0")].most_common(3)} }
p = Path(__file__).with_name("septa_subway.json")
p.write_text(json.dumps(out, indent=1))
print(f"wrote {p} ({p.stat().st_size:,} bytes)")
for k, v in lines.items(): print(f"{k}: {len(v)} stations, shape {len(geom[k])} pts")
for k, v in bsl_conn.items(): print(f"  {k}: {v}")
