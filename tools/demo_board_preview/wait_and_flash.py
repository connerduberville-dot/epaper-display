"""Waits (up to 30 min) for the demo board's STM32 bootloader on a COM port, then rebuilds with fresh SEPTA data
and flashes. Fit P12 and switch the board OFF then ON to trigger it."""
import serial, subprocess, sys, time
from pathlib import Path
port = sys.argv[1] if len(sys.argv) > 1 else "COM7"
here = Path(__file__).resolve().parent
end = time.time() + 1800
print(f"waiting for the bootloader on {port} ...", flush=True)
while time.time() < end:
    try:
        s = serial.Serial(port, 115200, parity=serial.PARITY_EVEN, timeout=0.3)
        s.reset_input_buffer(); s.write(b"\x7f"); r = s.read(1); s.close()
        if r and r[0] in (0x79, 0x1F):
            print("bootloader answered; building with live data and flashing", flush=True)
            sys.exit(subprocess.run([sys.executable, "-I", str(here / "build.py"), "--flash", port]).returncode)
    except serial.SerialException:
        pass                                   # board switched off / port gone for a moment
    time.sleep(1.0)
print("gave up waiting"); sys.exit(1)
