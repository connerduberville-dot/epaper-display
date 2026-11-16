# firmware/Display: base firmware with remote updates

The display shows, in order of priority:
1. A **picture pushed** from the web page or `POST /api/fb`. It stays until "Back to clock" or a restart.
2. A **content URL** frame, if one is set in Settings. The URL serves a raw 81,600-byte 1-bit frame and is re-fetched on an interval (ETag-aware).
3. Otherwise the **home screen** (`dashboard.h`): a greeting, the clock, the weather now, and a 24-hour
   panorama with night shaded from the real sun position, the temperature curve, chance of rain, sunrise/sunset,
   the moon phase and the daylight trend. A status footer shows the firmware build, IP and update status.
   - The forecast comes from [Open-Meteo](https://open-meteo.com), which is free and needs no key. It's fetched once an hour, so the panorama's hourly shift and the new data arrive in one full refresh. Within an hour, every change is a partial refresh under the ghost limit.
   - Name, latitude/longitude, units and weather on/off are under Settings, then Home screen.
   - Host checks: `hosttest/render_display.cpp` renders previews and reports the worst minute tick; `hosttest/weather_parse_test.cpp` checks the parser.
4. Or, with Settings, then Screen, one of two **SEPTA maps** (`subway.h`) for hanging the display vertically, each with a clock:
   - **Broad Street Line map** (the default): SEPTA's own BSL line map (Version 2.0) redrawn in black and white. It has:
     - the B1/B2/B3 strands, and black pills at transfer stations;
     - accessible stations and the reverse-direction marks;
     - bus routes beside each station (frequent routes outlined);
     - Regional Rail, L, T, G and PATCO connections, and the special-event and limited-service notes;
     - the clock in the header.
   - **All subway lines:** every Broad Street, Market-Frankford (L1) and trolley-tunnel (T1-T5) station, plus PATCO in Center City.
   - The map comes from SEPTA's official GTFS data. Rebuild with `tools/subway_map/extract.py`, then `make_bsl.py` and `make_map.py`. They write `subway_bsl.h` and `subway_map.h` and check for overlapping labels.
   - "When hung vertically" picks which way the panel is turned.

## Updating the firmware

| From | How |
|---|---|
| This PC | `python -I tools/fw.py build` then `python -I tools/fw.py lan` (Wi-Fi, ~10 s) or `... usb` (cable, COM6) |
| A phone on the home Wi-Fi | `http://epaper.local` (or `http://10.0.0.4`), Update tab: "Install latest" from GitHub, or upload a `.bin` |
| Anywhere (cloud session, phone) | Push to `main`. GitHub Actions builds, publishes to the `ota` release, and the board installs it within `checkMin` minutes |

**Safety:**
- A freshly installed image boots "pending". It must be on Wi-Fi for 60 s before it marks itself good.
- If it crashes, reboots, or can't reach Wi-Fi within 5 min, the board goes back to the previous firmware.
- Pulled updates are fetched over verified HTTPS, and their size and SHA-256 are checked before they're activated.

**Build numbers:**
- `tools/fw.py build` defaults to build 0, a dev build. Dev builds never auto-install from GitHub, so a test build isn't replaced behind your back. "Install latest" still works on them.
- CI builds use the workflow run number, so each published build is newer than the last.

## Settings (web UI, or `python -I tools/fw.py set key=value`)

`updateUrl`, `checkMin`, `autoUpdate`, `contentUrl`, `contentSec`, `tz`, `fullEveryN`, `fullMaxAgeMin`, `wifiSleep`, `adminPass`.

- If `adminPass` is set, changes and uploads need HTTP basic auth (user `admin`). `tools/fw.py` reads the password from the `EPAPER_ADMIN_PASS` environment variable.

## Pictures

- From a phone: web UI, Picture tab. The image is cropped to 1360x480 and dithered in the browser.
- From the PC: `python -I tools/fw.py frame photo.jpg` (push), or add `--out frame.bin` to save a file for a content URL.

## Files

| File | Purpose |
|---|---|
| `Display.ino` | Modes, refresh policy, Wi-Fi, web hooks |
| `ota.h` | Pull update, LAN upload, rollback, verified HTTPS client |
| `web_ui.h` | Page + JSON API |
| `screen.h` | Built-in screens (host-renderable: `hosttest/render_display.cpp`) |
| `content.h` | Content URL fetch |
| `settings.h` | Settings stored in NVS |
| `wifi_portal.h` | `EPaper-Setup` captive portal |
| `epd_qyeg1085.*`, `framebuffer.h`, `fonts.h` | Driver (same as `starter/HelloPanel`) |
| `partitions.csv` | Two 1.875 MB OTA app slots |
