"""Builds the panel bridge: STM32 firmware (out/bridge.bin) and the Windows driver DLL (out/panel.dll).
Needs: pip install ziglang
"""
import subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent.parent
VENDOR = PKG / "vendor" / "stm32_demo_20240926"
PREVIEW = HERE.parent / "demo_board_preview"          # reuses its startup.c and link.ld
OUT = HERE / "out"


def zig(*args):
    r = subprocess.run([sys.executable, "-m", "ziglang", *map(str, args)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("compiler failed")


def build():
    OUT.mkdir(exist_ok=True)
    common = ["-target", "thumb-freestanding-eabi", "-mcpu=cortex_m3", "-ffreestanding", "-fno-builtin",
              "-fno-sanitize=undefined", "-fno-stack-protector", "-DSTM32F10X_HD", "-DUSE_STDPERIPH_DRIVER",
              "-Wno-everything", "-I", VENDOR / "StartUp", "-I", VENDOR / "FWLib"]
    objs = []
    for src, opt in [(PREVIEW / "startup.c", "-O0"), (HERE / "stm32" / "bridge.c", "-O2"),
                     (VENDOR / "StartUp" / "system_stm32f10x.c", "-Os"),
                     (VENDOR / "FWLib" / "stm32f10x_gpio.c", "-Os"), (VENDOR / "FWLib" / "stm32f10x_rcc.c", "-Os")]:
        obj = OUT / (src.stem + ".o")
        zig("cc", *common, opt, "-c", "-o", obj, src)
        objs.append(obj)
    zig("cc", "-target", "thumb-freestanding-eabi", "-mcpu=cortex_m3", "-nostdlib", "-T", PREVIEW / "link.ld",
        "-Wl,--gc-sections", "-o", OUT / "bridge.elf", *objs)
    zig("objcopy", "-O", "binary", OUT / "bridge.elf", OUT / "bridge.bin")
    print(f"bridge firmware: out/bridge.bin, {(OUT / 'bridge.bin').stat().st_size} bytes")

    zig("c++", "-std=c++17", "-O2", "-shared", "-target", "x86_64-windows-gnu", "-I", HERE / "host" / "shim",
        "-o", OUT / "panel.dll", HERE / "host" / "panel.cpp")
    print("driver library: out/panel.dll")


if __name__ == "__main__":
    build()
