# 10.85" e-paper display kit: hardware context for Claude

This folder documents a working, assembled e-paper display: a Seeed XIAO ESP32-C6 driving a 1360x480 black/white e-paper panel. The panel, wiring and driver are all **proven on real glass**. Use this file as the source of truth for the hardware. The owner can ask you to show anything on it: a clock, weather, a calendar, images, a dashboard, and so on. Your job is to write the firmware (or a PC-side tool) that does it.

Owner: Conner. Work style: drive the work yourself and report plainly. Ask before spending money or downloading anything outside the normal toolchain. Never put Wi-Fi passwords or API keys in code: use a setup portal or the existing saved credentials (see Wi-Fi below).

---

## 1. What's physically there

```
 PC (Windows 11) --USB-C--> XIAO ESP32-C6 --9 thin soldered wires--> DESPI-C02 V3 adapter --24-pin FPC--> panel
                            (COM6)                                   (boost converter on board)
```

The unit is fully assembled in a 3D-printed case, black PLA, 282.8 x 125.5 mm:
- **Front:** the panel behind a bezel. The window is the active area plus 1.2 mm, so **every pixel is visible** (no hidden margin).
- **Back:** a bay holding the adapter and the XIAO under a printed cap (2 screws).
- **Mounting:** keyhole wall mount, or a slide-in desk foot.
- **Power and data:** a single USB-C cable into the XIAO, which exits at the back/bottom.
- **XIAO buttons:** BOOT and RESET are under the cap. Flashing normally needs neither (native USB, auto-reset). If a sketch ever breaks USB, take off the cap (2 screws), hold BOOT, tap RESET, then flash.

### Panel: Qiyun QYEG1085BNF86BF0 (also sold as 1085BNF686F0)
- 10.85", **1360 x 480 px**, 1-bit black/white.
- Glass 270.56 x 105.92 x 1.20 mm; active area 259.76 x 91.68 mm (about 0.191 mm/px, about 133 dpi).
- **Two JD79686BB controllers**, each driving 680 x 480:
  - **CS** drives the left half: columns 0-679, bytes 0-84 of each row.
  - **CS2** drives the right half: columns 680-1359, bytes 85-169.
- SCK, MOSI, D/C, RES and BUSY are shared by both controllers. **BUSY is LOW while busy.** SPI mode 0, MSB first, write-only (no MISO).
- **Waveforms:**
  - Full refresh uses the panel's built-in OTP waveform. It flashes and gives the best contrast.
  - Partial refresh uses a register LUT from the vendor code. No flash, but it slowly ghosts.
- **Measured on this unit with the XIAO:**
  - Full refresh: **4.5 s** (about 6.5 s including data transfer).
  - Partial refresh: **0.56 s**.
- E-paper holds the image with no power. Unplugging leaves the last image on screen.
- Datasheet: `vendor/docs/QYEG1085BNF86BF0-V2.0-panel-spec.pdf`. Pinout is on p.8.

### Adapter: DESPI-C02 V3 (silk "DESPI-C02V3 / QIYUN-DISPLAY")
- 53.3 x 31.2 mm, with an on-board boost converter for the panel's high voltages. It runs from 3.3 V.
- The RESE switch is set to **A (0.47 Ω)**. Leave it there.
- BS is tied to GND through R1, which selects 4-wire SPI.
- The schematic is in `vendor/docs/DESPI-C02-V3.1-adapter-schematic.pdf`.

### MCU: Seeed XIAO ESP32-C6
- RISC-V at 160 MHz, about 320 KB of usable RAM for the sketch, 4 MB flash.
- Wi-Fi 6 **2.4 GHz only**, plus BLE 5 and 802.15.4 (Thread/Zigbee).
- Native USB serial: it shows up as **COM6** on Conner's PC.

### Wiring (soldered, final, do not change)

| XIAO pin | Adapter | Notes |
|---|---|---|
| 3V3 | 3.3V | |
| GND | GND | |
| D10 (MOSI) | SDI | hardware SPI |
| D8 (SCK) | SCLK | hardware SPI |
| D1 | CS | left controller |
| D0 | CS2 | right controller; wired to the adapter's J1 pin |
| D2 | D/C | |
| D3 | RES | |
| D4 | BUSY | input, LOW = busy |

- **Free XIAO pins:** D5, D6, D7, D9.
  - D5 is used by the reference firmware as "hold LOW at power-on to force Wi-Fi setup". Nothing is physically wired to it.
  - These pins are inside the case. Adding a sensor or button means opening the bay.
- Wiring diagram: `drawings/wiring.pdf`. Board photos: `vendor/*_photo_nov11.jpg`.

### Also on hand: DESPI-M01VE demo board (optional bench tool)
- An STM32F103 board with a CH340 USB-serial chip (**COM7** on Conner's PC).
- It has the same 8-signal header and can drive the *same model* of panel. Conner's panel is now inside the case wired to the XIAO, so the demo board only matters if the panel is moved back to it.
- `tools/panel_bridge/` turns it into a PC-controlled panel driver; see section 6.

---

## 2. Framebuffer format (confirmed on glass)

- 1360 x 480, **1 bit per pixel, 1 = black, 0 = white**, MSB = leftmost pixel of each byte.
- Row stride 170 bytes; total **81,600 bytes**.
- Bytes 0-84 of each row go to the CS (left) controller, bytes 85-169 to the CS2 (right) controller.
- `(0,0)` is the top-left as you look at the board mounted normally (landscape, wide).
- Rotation is not handled by the driver. For portrait or 180°, transform when drawing.

---

## 3. The driver: `starter/HelloPanel/epd_qyeg1085.{h,cpp}`

This is a port of the vendor STM32 demo (`vendor/stm32_demo_20240926/EPD_W21/EPAPER.c`). Command order, LUTs and timing are verbatim, with one deliberate fix (see below). API:

```cpp
septa::EpdQyeg1085 epd;
epd.begin({D1 /*CS*/, D0 /*CS2*/, D2 /*DC*/, D3 /*RST*/, D4 /*BUSY*/});   // SPI 8 MHz default
epd.fullUpdate(fb);                                  // OTP waveform, flashes, ~4.5 s, clears ghosts
epd.partialUpdate(oldFb, newFb, leftRect, rightRect); // ~0.56 s, no flash; old = what's on glass now
epd.lastRefreshMs(); epd.lastBusyTimedOut();          // diagnostics
```

- **Partial updates need the previous image.** The controller drives each pixel from old to new. Keep a `shown` buffer that mirrors the glass, and copy `next` into it after each update.
- **Rects are in panel coordinates.**
  - Compute them with `next.diffRect(shown, 0, 680)` and `next.diffRect(shown, 680, 1360)`.
  - x and width are byte-aligned (multiples of 8).
  - An empty rect becomes a tiny no-op window.
- **The driver makes every partial window full height.** On this panel a window shorter than 480 rows draws a stray 1-px vertical line through the rows outside the window. Full height fixes that.
- **After each update the driver puts the panel into deep sleep.** Every update starts with a hardware reset and a full init, so there is no wake/sleep state to manage.
- **BUSY timeouts:** 15 s full, 6 s partial. The driver gives up instead of hanging. `lastBusyTimedOut()` true means a bad FPC seat or wiring, or the panel isn't powered.
- **Do NOT call `epd.clearWhite()`.** It allocates a third 81.6 KB static buffer. Clear your own buffer to white and call `fullUpdate` instead.

### Refresh policy that works on this panel (measured)
- **Ghosting:**
  - Partial updates leave a faint ghost where large black areas turn white.
  - Rule used: if a change turns **more than 1500 black pixels white**, do a full refresh instead. Use `next.clearedPixels(shown)`.
  - Example: a big "1 MIN LATE" pill changing to "ON TIME" cleared 11,483 px and visibly ghosted with partial.
- **Periodic full refresh:** also do a full refresh every ~12-30 partials, or at least hourly.
- **Only refresh on change.** If both diff rects are empty, skip the update entirely. E-paper wears with refresh count, and full refreshes flash.
- **No grayscale or colour support is implemented.** The panel is sold as B/W. Grayscale via custom LUTs might be possible but is untested. Dithering (Floyd-Steinberg or ordered) works fine for photos.

### `framebuffer.h`
- **Drawing:** `clear(black)`, `set/get(x,y)`, `fillRect`.
- **Text:** `text(x, baselineY, str, font, black)` returns the advance width; `textWidth(str, font)`.
- **Helpers:** `diffRect`, `clearedPixels`.
- Namespace `septa`, constants `W=1360 H=480 STRIDE=170 HALF_W=680 FB_BYTES=81600`.

### `fonts.h` (generated bitmap DejaVu)
- **ASCII 32-90 only:** space, digits, punctuation and A-Z. Lowercase is drawn as uppercase; anything else becomes `?`.

| index | font |
|---|---|
| 0 | Sans 22 px |
| 1 | Sans 28 px |
| 2 | Sans Bold 38 px |
| 3 | Sans Bold 54 px |
| 4 | Sans Bold 160 px |
| 5 | Sans Bold 120 px |

- The file is 500 KB of flash, which is fine (huge_app gives 3 MB).
- For lowercase, other sizes or other fonts, generate a new bitmap font: for example with Pillow on the PC, writing the same `Glyph{off,w,h,left,top,adv}` + bits layout. Adafruit_GFX fonts are another option.
- Ziglang/Python are available for generator scripts.

### RAM budget
- Two framebuffers use 163 KB of the C6's ~320 KB. HelloPanel compiles to 176 KB of globals, leaving ~150 KB.
- Wi-Fi plus HTTPS (TLS) needs roughly 50-80 KB of heap. That works (the reference firmware does it) but is not roomy.
- For big data, such as images fetched over HTTP, stream directly into the framebuffer row by row instead of buffering the whole download.

---

## 4. Toolchain on Conner's PC (Windows 11)

- **arduino-cli** is installed and on PATH. The esp32 core 3.1.3 and ArduinoJson 7.3.1 are installed through the `sketch.yaml` profile.
  - From the folder that contains the sketch folder:
    ```
    arduino-cli compile --profile xiao_c6 HelloPanel
    arduino-cli upload  --profile xiao_c6 -p COM6 HelloPanel
    ```
  - FQBN: `esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app`. The 3 MB app partition means **no OTA** (see section 7). HelloPanel alone is 0.42 MB, but the full reference firmware is 1.34 MB, which overflows the default 1.25 MB partition.
  - Each sketch folder needs its own `sketch.yaml`, as in `starter/HelloPanel/`.
- **Serial monitor:** 115200 baud.
  - Python with pyserial works for logging.
  - Opening the port toggles RTS/DTR, which can reset the C6. That's useful to catch the boot log.
  - Close the port before uploading.
- **No g++/MSVC.** Use `python -m ziglang c++ -std=c++17 ...` (pip package `ziglang`, installed) for host-side C++. Examples: rendering frames to PNG on the PC, unit tests in `hosttest/`.
- **Python 3** is installed. Use `python -I` for scripts.
- Shells available: Git Bash and PowerShell 5.1.

---

## 5. Current state of the device (as of Nov 16, 2026)

- **The XIAO runs `firmware/Display/`**, the OTA-capable base firmware (see `firmware/README.md`). It replaced the old train board, whose code is still in `reference_firmware/SeptaBoard/`.
  - It shows a clock + date with a status footer, a pushed picture, or a frame from a content URL.
  - Web UI and API: `http://epaper.local/` or `http://10.0.0.4/`. The UI also works from a phone on the same Wi-Fi.
- **Updating it never needs the USB cable now.** Use one of these:
  - `python -I tools/fw.py build` then `python -I tools/fw.py lan`: uploads over Wi-Fi in ~10 s.
  - Push to `main` on GitHub: Actions builds it, and the board pulls it (once the repo exists and `updateUrl` is set).
  - A new image must stay on Wi-Fi for 60 s or it rolls back to the previous slot. Verified on glass.
- **Partition table is custom** (`firmware/Display/partitions.csv`): two 1.875 MB app slots. Sketches built with the old `huge_app` profile (HelloPanel, SeptaBoard) still flash over USB, but they remove OTA until `firmware/Display` is flashed again.
- **Saved Wi-Fi:** the home network "Club Fred" (2.4 GHz), in NVS (Preferences namespace `"wifi"`, keys `"ssid"` and `"pass"`). The password is never stored in code or in this file.
  - NVS survives `arduino-cli upload`, so a new sketch can reuse the saved network.
  - The old saved network was Conner's phone hotspot. If Wi-Fi fails, the board shows the network name and opens the open AP **EPaper-Setup**. On the phone: join it, turn off mobile data, and browse to http://192.168.4.1 (the captive page does not always pop up).
- **Settings** live in NVS namespace `"disp"`: update URL, check interval, content URL, TZ, refresh policy and an optional admin password.

---

## 6. Reference code in this kit (reuse freely)

| Path | What it is |
|---|---|
| `firmware/Display/` | **What the board runs now.** OTA base firmware: GitHub pull updates, LAN upload, rollback, web UI, picture push, content URL, clock screen. Change what's shown in `renderClock()`/`screen.h` or add a renderer. Build and flash with `tools/fw.py`. |
| `tools/fw.py` | Build, flash over USB (`usb`) or Wi-Fi (`lan`), status, settings, actions, image to frame (`frame`), CI manifest. |
| `.github/workflows/firmware.yml` | CI: builds on push to `main`, publishes `firmware.bin` + `manifest.json` to the release tagged `ota`. |
| `starter/HelloPanel/` | **Minimal complete sketch**: driver, framebuffer, fonts, full and partial refresh, ghost rule. Compiles (checked Nov 16). Start here. |
| `reference_firmware/SeptaBoard/` | Full production firmware (v1.1.0). Reusable parts:<br>• `wifi_portal.h`: captive-portal Wi-Fi setup (open AP, form, saves to NVS).<br>• `web_ui.h`: on-device web page plus JSON API, including `GET /fb.bin`, which serves the live 81,600-byte framebuffer for previews.<br>• `settings.h`: runtime settings in NVS.<br>• `logbuf.h`: log ring shown in the web UI.<br>• `.ino`: refresh policy, NTP time with a TZ string, test pattern.<br>`layout_v2.h` and `rail_logic.h` are app-specific examples of a renderer. |
| `hosttest/` | PC tests. `mock/` has fake `Arduino.h` and `SPI.h` that record the SPI command stream, so driver changes can be checked against the vendor sequence with no hardware: `python -m ziglang c++ -std=c++17 -Imock driver_test.cpp`. |
| `tools/panel_bridge/` | Drives a panel from the PC through the **demo board** (921600-baud packet protocol; see the comment in `stm32/bridge.c`). `build.py` rebuilds `panel.dll` and the STM32 firmware with ziglang. `server.py` serves a web UI. Only relevant if a panel is connected to the demo board. |
| `tools/demo_board_preview/` | `stm32_uart_flash.py` is an STM32 UART bootloader flasher (AN3155) for the demo board. It also renders frames to the demo board. |
| `vendor/stm32_demo_20240926/` | Vendor C code (EPAPER.c/h = the authoritative command sequences, LUTs, partial-window code). |
| `vendor/docs/` | Panel spec, adapter schematic, demo board schematic, demo manual (Chinese), image-conversion guide (Chinese). |
| `drawings/` | Wiring diagram (PDF/SVG). |

### Good patterns for "show anything"
1. **Render on the device.** Fetch data over HTTPS (ArduinoJson for parsing), draw with `framebuffer.h`, refresh. This is what the reference firmware does.
2. **Render on a PC or server, push pixels.** Add an HTTP endpoint (for example `POST /fb`, 81,600 raw bytes) or have the XIAO fetch a `.bin`/1-bit PNG from a URL. Then do a diff-based partial or full update.
   - This makes layouts, photos, charts and arbitrary fonts easy: Python and Pillow on the PC, then threshold or dither to 1-bit.
   - Keep the same byte layout as section 2.
3. **Preview without the glass.** Serve `/fb.bin` and turn it into a PNG on the PC, or render the same drawing code on the PC with ziglang. This avoids wearing the panel while iterating on a layout.

---

## 7. Gotchas already learned (don't rediscover)
- **Handle the FPC with care.** Never plug or unplug it while powered. Contacts face the display side, pin 1 (CS2) is at the J1 end, and the latch is a back-flip type.
- **A BUSY timeout on every update** means the FPC is unseated or not latched, or a wire is broken. A BUSY timeout only on the right half means CS2 (D0 to J1).
- **Image looks wrong:**
  - Half-swapped image: the byte offset per controller is wrong.
  - Inverted image: the 1 = black convention was flipped.
  - Both were verified correct with a test pattern (`paintTestPattern()` in the reference `.ino`), so if you see either, the new code is at fault.
- **Partial windows shorter than 480 rows draw a stray line.** The driver already fixes this; keep the fix.
- **Large black-to-white changes ghost under partial refresh.** Use a full refresh for those (section 3).
- **Avoid a third framebuffer** (no `clearWhite()`). Watch heap when adding TLS.
- **ESP32-C6 Wi-Fi is 2.4 GHz only.** Captive-portal or hotel-style networks won't work.
- **Partition and OTA:** the XIAO_ESP32C6 board menu has no `min_spiffs` option. `firmware/Display` uses a `partitions.csv` in the sketch folder plus `--build-property upload.maximum_size=1966080` (`tools/fw.py` does both). It is ~1.46 MB.
- **A big clock ghosts.** At 120-160 px, about a third to half of all minute ticks clear more than 1,500 px, which would force a full refresh. At 54 px the worst tick clears 682 px (`hosttest/clock_ticks.cpp`).
- **TLS:** use the Mozilla bundle built into the core (`secureClient()` in `firmware/Display/ota.h`), not `setInsecure()`. Tested: GitHub OK, self-signed cert refused. Minimum free heap during HTTPS was ~79 KB.
