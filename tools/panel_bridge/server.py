"""Fort Washington board, PC edition: runs the board's logic on this PC and drives the real panel through the
Qiyun demo board (no XIAO needed). Same web UI and JSON API as the XIAO firmware (served from web_ui.h).

  python -I server.py [--port COM7] [--http 8080]
  then open http://127.0.0.1:8080

Panel path: panel.dll (firmware's own layout_v2.h + epd_qyeg1085.cpp driver) -> USB serial -> STM32 bridge
(stm32/bridge.c) on the demo board -> DESPI-C02 adapter -> panel.
Demo board setup: P12 (BOOT0) jumper ON. On every power-up the STM32 waits in its ROM bootloader; this server
loads the bridge if it isn't there yet and starts it. No jumper moves needed after that.
"""
import argparse, ctypes, datetime, json, re, sys, threading, time, traceback, urllib.parse, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial

HERE = Path(__file__).resolve().parent
PKG = HERE.parent.parent
sys.path.insert(0, str(HERE.parent / "demo_board_preview"))
from stm32_uart_flash import Bootloader, FLASH_BASE  # noqa: E402

FB_BYTES = 170 * 480
FW = "PC bridge 1.0 (layout_v2.h + epd_qyeg1085.cpp)"
START = time.time()

# ------------------------------------------------------------------ log (mirrors the firmware's LOG ring)
_log, _log_lock = [], threading.Lock()


def log(msg):
    line = f"{int(time.time() - START):6d} {msg}"
    print(line, flush=True)
    with _log_lock:
        _log.append(line)
        del _log[:-300]


# ------------------------------------------------------------------ settings (same fields/limits as settings.h)
DEFAULTS = dict(pollSec=60, fullEveryN=12, fullMaxAgeMin=60, staleMin=15, nightStart=1, nightEnd=5, nightPollSec=600,
                origin="Ft Washington", dest="Suburban Station", wifiSleep=True, cpu80=False, devMode=True)
SETTINGS_FILE = HERE / "settings.json"


def clamp(s):
    lim = dict(pollSec=(15, 3600), fullEveryN=(1, 200), fullMaxAgeMin=(5, 1440), staleMin=(2, 240),
               nightStart=(0, 23), nightEnd=(0, 23), nightPollSec=(60, 3600))
    for k, (lo, hi) in lim.items():
        s[k] = max(lo, min(hi, int(s[k])))
    for k in ("origin", "dest"):
        s[k] = str(s[k])[:31] or DEFAULTS[k]
    for k in ("wifiSleep", "cpu80", "devMode"):
        s[k] = bool(s[k])
    return s


def load_settings():
    s = dict(DEFAULTS)
    try:
        s.update(json.loads(SETTINGS_FILE.read_text()))
    except Exception:
        pass
    return clamp(s)


cfg = load_settings()


def save_settings():
    SETTINGS_FILE.write_text(json.dumps(cfg, indent=1))


def is_night(hour):
    a, b = cfg["nightStart"], cfg["nightEnd"]
    if a == b:
        return False
    return a <= hour < b if a < b else (hour >= a or hour < b)


# ------------------------------------------------------------------ panel.dll
dll = ctypes.CDLL(str(HERE / "out" / "panel.dll"))
dll.pb_open.argtypes = [ctypes.c_char_p]
dll.pb_error.restype = ctypes.c_char_p
dll.pb_paint.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
dll.pb_render_board.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.pb_describe.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
dll.pb_render_pattern.argtypes = [ctypes.c_char_p]
render_lock, panel_lock = threading.Lock(), threading.Lock()


def parse_described(text):
    out = []
    for line in text.splitlines():
        f = line.split("|")
        if len(f) >= 8:
            out.append(dict(number=f[0], sched=f[1], est=f[2], status=f[3], delayRaw=f[4], lateMin=int(f[5]),
                            direct=f[6] == "1", inMin=int(f[7])))
    return out


def render_board(trains_text, now_min, clock, updated, fault=None, detail=None):
    fb = ctypes.create_string_buffer(FB_BYTES)
    shown = ctypes.create_string_buffer(4096)
    with render_lock:
        dll.pb_render_board(trains_text.encode(), now_min, clock.encode(), updated.encode(),
                            (fault or "").encode(), (detail or "").encode(), fb, shown, 4096)
    return fb.raw, parse_described(shown.value.decode())


def describe(trains_text, now_min):
    out = ctypes.create_string_buffer(8192)
    with render_lock:
        dll.pb_describe(trains_text.encode(), now_min, out, 8192)
    return parse_described(out.value.decode())


def render_pattern():
    fb = ctypes.create_string_buffer(FB_BYTES)
    with render_lock:
        dll.pb_render_pattern(fb)
    return fb.raw


# ------------------------------------------------------------------ demo board connection
class Panel:
    def __init__(self, port):
        self.port, self.connected, self.note = port, False, "starting"
        self.bridge = (HERE / "out" / "bridge.bin").read_bytes()

    def _bootloader_start(self):
        """If the STM32 is in its ROM bootloader: load the bridge (if missing) and start it. True if started."""
        try:
            bl = Bootloader(self.port)
            bl.s.timeout = 0.3
        except serial.SerialException as e:
            note = f"{self.port} not available: {str(e)[-60:]}"
            if note != self.note:
                log(f"[bridge] {note}")
            self.note = note
            return False
        try:
            try:
                bl.sync()
            except RuntimeError:
                return False
            bl.s.timeout = 2
            ver, cmds = bl.get()
            img = self.bridge
            current = b"".join(bl.read(FLASH_BASE + o, min(256, len(img) - o)) for o in range(0, len(img), 256))
            if current != img:
                log(f"[bridge] loading bridge firmware ({len(img)} bytes) into the demo board")
                bl.erase_all(cmds)
                for o in range(0, len(img), 256):
                    chunk = img[o:o + 256]
                    chunk += b"\xff" * (-len(chunk) % 4)
                    bl.write(FLASH_BASE + o, chunk)
                if b"".join(bl.read(FLASH_BASE + o, min(256, len(img) - o)) for o in range(0, len(img), 256)) != img:
                    raise RuntimeError("verify failed")
                log("[bridge] loaded and verified")
            bl.go(FLASH_BASE)
            log("[bridge] started from the STM32 bootloader")
            return True
        except Exception as e:
            log(f"[bridge] bootloader step failed: {e}")
            return False
        finally:
            bl.s.close()

    def connect(self):
        with panel_lock:
            dll.pb_close()
            started = self._bootloader_start()
            time.sleep(0.3 if started else 0.1)
            if dll.pb_open(self.port.encode()):
                self.connected, self.note = True, "connected"
                dll.pb_forget_shown()
                log(f"[bridge] connected on {self.port}")
                return True
            err = dll.pb_error().decode()
            dll.pb_close()
            self.connected = False
            if not self.note.startswith(self.port + " not"):
                note = "waiting: with P12 on, switch the demo board OFF and ON"
                if note != self.note:
                    log(f"[bridge] {note} (bridge ping: {err})")
                self.note = note
            if started:
                log(f"[bridge] started but not answering: {err}")
            return False


# ------------------------------------------------------------------ board state + poll (mirrors SeptaBoard.ino)
S = dict(feed="booting", trains_text="", fetched=0, last_good=None, updated="", shown=[], last_poll=0.0,
         interval=60, night=False, sim=None, epd=dict(kind="none", ms=0, refreshMs=0, busyTimeout=False),
         partials=0, fb=bytes(FB_BYTES), pending=None)
wake = threading.Event()
panel = None


def clock_str(dt):
    return dt.strftime("%I:%M %p").lstrip("0")


def fetch():
    url = ("https://www3.septa.org/api/NextToArrive/index.php?req1=" + urllib.parse.quote(cfg["origin"])
           + "&req2=" + urllib.parse.quote(cfg["dest"]) + "&req3=8")
    with urllib.request.urlopen(url, timeout=15) as r:
        rows = json.load(r)
    if not isinstance(rows, list):
        raise RuntimeError("INVALID FEED")
    return "\n".join(f"{t['orig_train']}|{t['orig_departure_time']}|{t.get('orig_delay', '')}|"
                     f"{1 if str(t.get('isdirect', 'true')).lower() == 'true' else 0}" for t in rows), len(rows)


def paint(fb, force_full=False):
    """Send a frame to the panel with the firmware's policy. Keeps it pending if the panel is offline."""
    if not (panel and panel.connected):
        S["pending"] = (fb, force_full)
        return False
    info = (ctypes.c_int * 5)()
    with panel_lock:
        ok = dll.pb_paint(fb, 1 if force_full else 0, cfg["fullEveryN"], cfg["fullMaxAgeMin"], info)
        err = dll.pb_error().decode() if not ok else ""
    if not ok:
        log(f"[epd] panel link lost: {err}")
        panel.connected, panel.note = False, f"lost: {err}"
        S["pending"] = (fb, True)
        wake.set()
        return False
    S["pending"] = None
    if info[0]:
        S["fb"] = bytes(fb)
        S["epd"] = dict(kind="full" if info[0] == 2 else "partial", ms=info[1], refreshMs=info[2], busyTimeout=bool(info[3]))
        log(f"[epd] {'FULL' if info[0] == 2 else 'partial'} update in {info[1]} ms (refresh wait {info[2]} ms)"
            f"{'  ** BUSY TIMEOUT **' if info[3] else ''}")
    S["partials"] = info[4]
    return True


def poll(force_full=False):
    err = None
    if S["sim"] is not None:
        text, n = S["sim"], len(S["sim"].splitlines())
    else:
        try:
            text, n = fetch()
        except Exception as e:
            err, text, n = (str(e)[:40] or "FEED ERROR"), None, -1
    now = datetime.datetime.now()
    if n >= 0:
        S.update(trains_text=text, fetched=n, last_good=time.time(), updated=clock_str(now))
    now_min = now.hour * 60 + now.minute
    S["night"] = is_night(now.hour)
    S["interval"] = cfg["nightPollSec"] if S["night"] else cfg["pollSec"]
    stale = not S["last_good"] or time.time() - S["last_good"] > cfg["staleMin"] * 60
    fault = "FEED UNAVAILABLE" if (n < 0 and stale) else None
    fb, shown = render_board(S["trains_text"], now_min, clock_str(now), S["updated"], fault, err if fault else None)
    S["shown"] = shown
    S["feed"] = err or "ok"
    log(f"[poll] {S['feed']}, {S['fetched']} trains, showing {len(shown)}"
        f"{' (simulated)' if S['sim'] is not None else ''}{' (night)' if S['night'] else ''}")
    S["last_poll"] = time.time()
    paint(fb, force_full)
    return fb


def worker():
    while True:
        try:
            if not (panel and panel.connected):
                if panel.connect() and S["pending"]:
                    paint(*S["pending"])
            due = time.time() - S["last_poll"] >= S["interval"]
            if due:
                poll()
        except Exception:
            log("[worker] " + traceback.format_exc().splitlines()[-1])
        wake.wait(1 if not (panel and panel.connected) else 2)
        wake.clear()


# ------------------------------------------------------------------ HTTP (same routes as web_ui.h)
UI_HTML = re.search(r'R"HTML\((.*?)\)HTML"', (PKG / "firmware" / "SeptaBoard" / "web_ui.h").read_text(encoding="utf-8"),
                    re.S).group(1)


def status_json():
    up = time.time() - START
    since = time.time() - S["last_poll"]
    return dict(
        time=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        ssid=f"demo board on {panel.port}: {panel.note}", rssi=-40 if panel.connected else -100,
        ip="PC bridge (127.0.0.1)", feed=S["feed"], sim=S["sim"] is not None, fetched=S["fetched"],
        shown=len(S["shown"]), lastGoodS=int(time.time() - S["last_good"]) if S["last_good"] else None,
        nextPollS=max(0, int(S["interval"] - since)), night=S["night"], partials=S["partials"], uptime=int(up),
        heap=0, cpuMhz=72, fw=FW, epd=S["epd"], trains=S["shown"], devMode=cfg["devMode"], wifiSleep=cfg["wifiSleep"])


def do_action(a):
    log(f"[ui] action {a}")
    if a == "poll":
        poll()
    elif a == "full":
        poll(force_full=True)
    elif a == "pattern":
        paint(render_pattern(), True)
    elif a == "white":
        paint(bytes(FB_BYTES), True)
        dll.pb_forget_shown()
    elif a == "reboot":
        panel.connected, panel.note = False, "reconnecting"
        wake.set()
    elif a == "resetwifi":
        return True, "Not used on the PC bridge (no Wi-Fi on the demo board)"
    else:
        return False, f"Unknown action: {a}"
    if not panel.connected:
        return True, f"Done: {a} (panel offline; it will be drawn when the demo board connects)"
    return True, f"Done: {a}"


def simulate(rows):
    lines = []
    for r in rows:
        dep = str(r.get("departure", ""))
        if dep:
            lines.append(f"{r.get('number', '0000')}|{dep}|{r.get('delay', 'On time')}|1")
    S["sim"] = "\n".join(lines) if lines else None
    log(f"[sim] {'on' if S['sim'] is not None else 'off'} ({len(lines)} trains)")
    poll()


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="text/plain"):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n).decode() if n else ""

    def do_GET(self):
        p = urllib.parse.urlparse(self.path).path
        if p == "/":
            self.send(200, UI_HTML, "text/html")
        elif p == "/api/status":
            self.send(200, json.dumps(status_json()), "application/json")
        elif p == "/api/settings":
            self.send(200, json.dumps(cfg), "application/json")
        elif p == "/fb.bin":
            self.send(200, S["fb"], "application/octet-stream")
        elif p == "/api/feed":
            now = datetime.datetime.now()
            self.send(200, json.dumps(describe(S["trains_text"], now.hour * 60 + now.minute)), "application/json")
        elif p == "/api/log":
            with _log_lock:
                self.send(200, "\n".join(_log) + "\n")
        else:
            self.send(404, "not found")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        p, q = u.path, urllib.parse.parse_qs(u.query)
        try:
            if p == "/api/settings":
                cfg.update(json.loads(self.body() or "{}"))
                clamp(cfg); save_settings()
                log(f"[cfg] settings saved (poll {cfg['pollSec']}s, full every {cfg['fullEveryN']}/{cfg['fullMaxAgeMin']}min, "
                    f"dev {int(cfg['devMode'])})")
                S["interval"] = cfg["nightPollSec"] if S["night"] else cfg["pollSec"]
                self.send(200, "ok")
            elif p == "/api/settings/reset":
                cfg.clear(); cfg.update(DEFAULTS); save_settings()
                log("[cfg] settings reset to defaults")
                self.send(200, "ok")
            elif p == "/api/action":
                a = (q.get("do") or [""])[0]
                if a not in ("poll", "resetwifi") and not cfg["devMode"]:
                    return self.send(403, "developer mode is off")
                ok, msg = do_action(a)
                self.send(200 if ok else 400, msg)
            elif p == "/api/simulate":
                if not cfg["devMode"]:
                    return self.send(403, "developer mode is off")
                rows = json.loads(self.body() or "[]")
                if not isinstance(rows, list):
                    return self.send(400, "bad json")
                simulate(rows)
                self.send(200, "ok")
            else:
                self.send(404, "not found")
        except json.JSONDecodeError:
            self.send(400, "bad json")


def main():
    global panel
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM7")
    ap.add_argument("--http", type=int, default=8080)
    a = ap.parse_args()
    panel = Panel(a.port)
    log(f"Fort Washington board, PC bridge. Demo board on {a.port}.")
    threading.Thread(target=worker, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", a.http), H)
    log(f"[ui] http://127.0.0.1:{a.http}/")
    srv.serve_forever()


if __name__ == "__main__":
    main()
