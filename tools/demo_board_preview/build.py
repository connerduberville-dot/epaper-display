"""Builds the demo-board preview: live SEPTA fetch -> frames rendered by layout_v2.h -> STM32 firmware .bin.

Needs:  pip install ziglang   (one self-contained C/C++ compiler for both the PC renderer and the STM32)
Usage:  python build.py [--frames 5] [--period 60] [--dest "Suburban Station"] [--flash COM7]
Output: out/preview.bin, out/frameN.png previews.
"""
import argparse, datetime, json, os, subprocess, sys, urllib.parse, urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent.parent
VENDOR = PKG / "vendor" / "stm32_demo_20240926"
OUT = HERE / "out"
FB_BYTES = 170 * 480


def zig(*args):
    cmd = [sys.executable, "-m", "ziglang", *map(str, args)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit(f"compiler failed: {' '.join(cmd[3:6])} ...")
    return r


def fetch(origin, dest):
    url = ("https://www3.septa.org/api/NextToArrive/index.php?req1=" + urllib.parse.quote(origin)
           + "&req2=" + urllib.parse.quote(dest) + "&req3=8")
    with urllib.request.urlopen(url, timeout=20) as r:
        rows = json.load(r)
    if not isinstance(rows, list):
        raise SystemExit(f"SEPTA said: {rows}")
    lines = []
    for t in rows:
        direct = str(t.get("isdirect", "true")).lower() == "true"
        lines.append(f"{t['orig_train']}|{t['orig_departure_time']}|{t.get('orig_delay', '')}|{1 if direct else 0}")
    return lines


def clock(m):
    h, mm = divmod(m % 1440, 60)
    return f"{(h + 11) % 12 + 1}:{mm:02d} {'AM' if h < 12 else 'PM'}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=5)
    ap.add_argument("--period", type=int, default=60, help="seconds between partial refreshes")
    ap.add_argument("--origin", default="Ft Washington")
    ap.add_argument("--dest", default="Suburban Station")
    ap.add_argument("--lead", type=int, default=2, help="minutes added to 'now' to cover build + flash time")
    ap.add_argument("--flash", metavar="COM", help="flash right after building, e.g. COM7")
    a = ap.parse_args()
    if a.frames * FB_BYTES > 470 * 1024:
        raise SystemExit("too many frames for 512 KB flash (max 5)")
    OUT.mkdir(exist_ok=True)

    # 1. host renderer (the firmware's own layout code)
    exe = OUT / "render_frames.exe"
    zig("c++", "-std=c++17", "-O2", "-o", exe, HERE / "render_frames.cpp")

    # 2. live data -> frames
    now = datetime.datetime.now()
    trains = fetch(a.origin, a.dest)
    (OUT / "trains.txt").write_text("\n".join(trains) + "\n")
    print(f"SEPTA {a.origin} -> {a.dest}: {len(trains)} trains")
    start = now.hour * 60 + now.minute + a.lead
    r = subprocess.run([exe, OUT / "trains.txt", str(start), str(a.frames), clock(now.hour * 60 + now.minute), OUT],
                       capture_output=True, text=True, check=True)
    print(r.stdout.strip())

    # PNG previews (optional: Pillow)
    try:
        from PIL import Image
        for k in range(a.frames):
            b = (OUT / f"frame{k}.bin").read_bytes()
            Image.frombytes("1", (1360, 480), bytes(~x & 0xFF for x in b)).save(OUT / f"frame{k}.png")
    except ImportError:
        pass

    # 3. frames -> C array in flash
    with open(OUT / "frames.h", "w") as h:
        h.write(f"#pragma once\n#define FRAME_COUNT {a.frames}\n#define FRAME_PERIOD_S {a.period}\n"
                f"extern const unsigned char FRAMES[FRAME_COUNT][{FB_BYTES}];\n")
    with open(OUT / "frames.c", "w") as c:
        c.write(f'#include "frames.h"\nconst unsigned char FRAMES[FRAME_COUNT][{FB_BYTES}] = {{\n')
        for k in range(a.frames):
            b = (OUT / f"frame{k}.bin").read_bytes()
            c.write("{" + ",".join(str(v) for v in b) + "},\n")
        c.write("};\n")

    # 4. STM32 firmware
    common = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m3", "-ffreestanding", "-fno-builtin", "-fno-sanitize=undefined", "-fno-stack-protector",
              "-DSTM32F10X_HD", "-DUSE_STDPERIPH_DRIVER", "-Wno-everything",
              "-I", VENDOR / "StartUp", "-I", VENDOR / "FWLib", "-I", VENDOR / "EPD_W21", "-I", OUT]
    objs = []
    for src, opt in [(HERE / "startup.c", "-O0"), (HERE / "main.c", "-Os"), (OUT / "frames.c", "-Os"),
                     (VENDOR / "EPD_W21" / "EPAPER.c", "-O0"),      # vendor delay loops rely on not being optimised away
                     (VENDOR / "StartUp" / "system_stm32f10x.c", "-Os"),
                     (VENDOR / "FWLib" / "stm32f10x_gpio.c", "-Os"), (VENDOR / "FWLib" / "stm32f10x_rcc.c", "-Os")]:
        obj = OUT / (src.stem + ".o")
        zig("cc", *common, opt, "-c", "-o", obj, src)
        objs.append(obj)
    elf = OUT / "preview.elf"
    zig("cc", "-target", "thumb-freestanding-eabi", "-mcpu=cortex_m3", "-nostdlib", "-T", HERE / "link.ld",
        "-Wl,--gc-sections", "-o", elf, *objs)
    zig("objcopy", "-O", "binary", elf, OUT / "preview.bin")
    size = (OUT / "preview.bin").stat().st_size
    print(f"firmware: out/preview.bin, {size // 1024} KB")
    if size > 512 * 1024:
        raise SystemExit("image larger than flash")

    if a.flash:
        subprocess.run([sys.executable, HERE / "stm32_uart_flash.py", a.flash, OUT / "preview.bin"], check=True)


if __name__ == "__main__":
    main()
