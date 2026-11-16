"""Flash an STM32F1 through its built-in UART bootloader (ST AN3155) over the demo board's CH340 USB port.

The DESPI-M01VE does not wire the CH340's DTR/RTS to NRST/BOOT0, so enter the bootloader by hand:
fit the P12 jumper (BOOT0 high), then switch the board OFF and ON. After flashing, this script sends
GO so the new program starts immediately. Remove P12 afterwards or the next power-up stays in the bootloader.

Usage:  python stm32_uart_flash.py COM7 firmware.bin [--hex] [--no-verify]
        (a .hex file, e.g. the vendor's mdk.hex, restores the original demo)
"""
import sys, time, argparse
import serial

ACK, NACK = 0x79, 0x1F
FLASH_BASE = 0x08000000


class Bootloader:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, bytesize=8, parity=serial.PARITY_EVEN, stopbits=1, timeout=2)

    def _ack(self, what, timeout=2.0):
        self.s.timeout = timeout
        b = self.s.read(1)
        if not b:
            raise RuntimeError(f"{what}: no reply (is P12 fitted and the board power-cycled?)")
        if b[0] != ACK:
            raise RuntimeError(f"{what}: NACK 0x{b[0]:02X}")

    def _cmd(self, c, what, timeout=2.0):
        self.s.write(bytes([c, c ^ 0xFF]))
        self._ack(what, timeout)

    def sync(self):
        self.s.reset_input_buffer()
        for _ in range(5):
            self.s.write(b"\x7f")
            b = self.s.read(1)
            if b and b[0] in (ACK, NACK):   # NACK = already synced from an earlier attempt
                return
            time.sleep(0.2)
        raise RuntimeError("no answer to 0x7F: fit P12, switch the demo board OFF then ON, and retry")

    def get(self):
        self._cmd(0x00, "GET")
        n = self.s.read(1)[0]
        data = self.s.read(n + 1)
        self._ack("GET end")
        return data[0], list(data[1:])

    def get_id(self):
        self._cmd(0x02, "GET ID")
        n = self.s.read(1)[0]
        pid = self.s.read(n + 1)
        self._ack("GET ID end")
        return int.from_bytes(pid, "big")

    def _addr(self, a):
        b = a.to_bytes(4, "big")
        x = b[0] ^ b[1] ^ b[2] ^ b[3]
        self.s.write(b + bytes([x]))

    def erase_all(self, cmds):
        if 0x44 in cmds:
            self._cmd(0x44, "EXT ERASE")
            self.s.write(b"\xff\xff\x00")
        else:
            self._cmd(0x43, "ERASE")
            self.s.write(b"\xff\x00")
        self._ack("mass erase", timeout=40)

    def readout_unprotect(self):
        self._cmd(0x92, "READOUT UNPROTECT")
        self._ack("readout unprotect done", timeout=40)

    def write(self, addr, data):
        self._cmd(0x31, "WRITE")
        self._addr(addr); self._ack("WRITE addr")
        n = len(data) - 1
        x = n
        for v in data:
            x ^= v
        self.s.write(bytes([n]) + data + bytes([x]))
        self._ack("WRITE data", timeout=3)

    def read(self, addr, n):
        self._cmd(0x11, "READ")
        self._addr(addr); self._ack("READ addr")
        self.s.write(bytes([n - 1, (n - 1) ^ 0xFF])); self._ack("READ len")
        return self.s.read(n)

    def go(self, addr):
        self._cmd(0x21, "GO")
        self._addr(addr); self._ack("GO addr")


def load_hex(path):
    """Intel HEX -> (start address, contiguous bytes, gaps filled with 0xFF)."""
    mem, upper = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        n, addr, typ, data = rec[0], int.from_bytes(rec[1:3], "big"), rec[3], rec[4:4 + rec[0]]
        if typ == 0:
            for i, v in enumerate(data):
                mem[upper + addr + i] = v
        elif typ == 4:
            upper = int.from_bytes(data, "big") << 16
        elif typ == 2:
            upper = int.from_bytes(data, "big") << 4
    lo, hi = min(mem), max(mem)
    return lo, bytes(mem.get(a, 0xFF) for a in range(lo, hi + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port"); ap.add_argument("image")
    ap.add_argument("--no-verify", action="store_true")
    a = ap.parse_args()
    if a.image.lower().endswith(".hex"):
        base, img = load_hex(a.image)
    else:
        base, img = FLASH_BASE, open(a.image, "rb").read()
    print(f"image: {len(img)} bytes at 0x{base:08X}")

    bl = Bootloader(a.port)
    bl.sync()
    ver, cmds = bl.get()
    print(f"bootloader v{ver >> 4}.{ver & 15}, chip id 0x{bl.get_id():03X}")
    try:
        bl.erase_all(cmds)
    except RuntimeError as e:
        print(f"erase refused ({e}); clearing read protection (mass-erases), then retrying")
        bl.readout_unprotect()
        time.sleep(1.0)
        bl = Bootloader(a.port); bl.sync(); ver, cmds = bl.get(); bl.erase_all(cmds)
    print("erased")

    t0 = time.time()
    for off in range(0, len(img), 256):
        chunk = img[off:off + 256]
        if len(chunk) % 4:
            chunk += b"\xff" * (4 - len(chunk) % 4)
        bl.write(base + off, chunk)
        if off % (32 * 1024) == 0:
            print(f"  wrote {off // 1024} KB", flush=True)
    print(f"written in {time.time() - t0:.0f} s")

    if not a.no_verify:
        for off in range(0, len(img), 256):
            want = img[off:off + 256]
            got = bl.read(base + off, len(want))
            if got != want:
                raise SystemExit(f"VERIFY FAILED at 0x{base + off:08X}")
        print("verified")

    bl.go(FLASH_BASE)
    print("started (remember to remove P12 before the next power-up)")


if __name__ == "__main__":
    main()
