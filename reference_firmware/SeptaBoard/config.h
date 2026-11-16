// Board configuration. Wi-Fi credentials are NOT here: they are entered once through the setup portal
// and stored in flash (Preferences). See README.
#pragma once

// ---- Pins (XIAO ESP32-C6 silkscreen labels) -> DESPI-C02 adapter ----
// D8  SCK  -> SCLK      D10 MOSI -> SDI      (hardware SPI, set in epd_qyeg1085.cpp)
#define PIN_EPD_CS    D1   // -> CS   (8-pin header)
#define PIN_EPD_CS2   D0   // -> CS2  (single pin J1 next to the FPC socket)
#define PIN_EPD_DC    D2   // -> D/C
#define PIN_EPD_RST   D3   // -> RES
#define PIN_EPD_BUSY  D4   // -> BUSY
// 3V3 -> 3.3V, GND -> GND

// ---- Station / route ----
#define SEPTA_ORIGIN       "Ft Washington"       // SEPTA's spelling for the API
#define SEPTA_DESTINATION  "Suburban Station"
#define SEPTA_RESULTS      8
#define TIMEZONE           "EST5EDT,M3.2.0,M11.1.0"

// ---- Timing ----
#define POLL_INTERVAL_MS        60000UL     // fetch SEPTA once a minute
#define FULL_REFRESH_EVERY_N    12          // partial updates between full (ghost-clearing) refreshes
#define FULL_REFRESH_MAX_AGE_MS (60UL*60UL*1000UL)  // ...or at least once an hour
#define FULL_REFRESH_IF_CLEARED_PX 1500       // a change that turns more black pixels white than this gets a full
                                             // refresh (partial would ghost). Minute ticks clear <= ~560 px; a status
                                             // pill change ~11,500; a new next-train time ~8,500 (measured Nov 9)
#define FEED_STALE_AFTER_MS     (15UL*60UL*1000UL)  // show FEED UNAVAILABLE after 15 min without data
#define WIFI_CONNECT_TIMEOUT_MS 20000UL

// ---- Setup portal ----
#define SETUP_AP_NAME      "SEPTA-Board-Setup"   // open network; join it, then any page opens the form
#define SETUP_BUTTON_PIN   D5                    // hold LOW (to GND) at power-on to force setup mode
