/* HelloPanel: the smallest complete program for this hardware.
 * Board:   Seeed XIAO ESP32-C6 (esp32 core 3.x, board XIAO_ESP32C6)
 * Display: Qiyun QYEG1085BNF86BF0 10.85" 1360x480 B/W on a DESPI-C02 V3 adapter
 *
 * What it does: one full refresh with a border, labels and a filled box, then a partial refresh of a
 * counter every 10 seconds. Use it as the starting point for anything else you want to show.
 *
 * Build/flash (run from the starter folder):
 *   arduino-cli compile --profile xiao_c6 HelloPanel
 *   arduino-cli upload  --profile xiao_c6 -p COM6 HelloPanel
 */
#include "framebuffer.h"
#include "epd_qyeg1085.h"

using namespace septa;

// Wiring (soldered, do not change): XIAO D1->CS, D0->CS2, D2->D/C, D3->RES, D4->BUSY, D8->SCLK, D10->SDI.
static EpdQyeg1085 epd;
static Framebuffer shown;   // what the glass shows now
static Framebuffer next;    // what we want it to show
// Two 81,600-byte buffers are fine on the C6. Do NOT call epd.clearWhite(): it allocates a third one.

static void drawStatic(Framebuffer& fb) {
  fb.clear(false);                                     // white
  fb.fillRect(0, 0, W, 4, true); fb.fillRect(0, H - 4, W, 4, true);
  fb.fillRect(0, 0, 4, H, true); fb.fillRect(W - 4, 0, 4, H, true);
  fb.text(40, 90, "HELLO PANEL", 3, true);              // font 3 = DejaVu Sans Bold 54 px, baseline at y
  fb.text(40, 140, "1360 X 480, 1 BIT, TWO CONTROLLERS", 1, true);
  fb.fillRect(900, 40, 420, 120, true);
  fb.text(930, 120, "INVERTED", 3, false);              // white text on black
}

static void drawCounter(Framebuffer& fb, unsigned n) {
  fb.fillRect(40, 220, 1280, 220, false);              // erase the counter area
  char buf[16]; snprintf(buf, sizeof buf, "%u", n);
  fb.text(40, 420, buf, 4, true);                       // font 4 = Bold 160 px
}

static void commit(bool full) {
  Rect l = next.diffRect(shown, 0, HALF_W), r = next.diffRect(shown, HALF_W, W);
  if (full) epd.fullUpdate(next);
  else if (!l.empty() || !r.empty()) epd.partialUpdate(shown, next, l, r);
  else return;
  memcpy(shown.px, next.px, FB_BYTES);
  Serial.printf("%s refresh %lu ms%s\n", full ? "full" : "partial", (unsigned long)epd.lastRefreshMs(),
                epd.lastBusyTimedOut() ? "  BUSY TIMEOUT (check wiring/FPC)" : "");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  epd.begin({D1, D0, D2, D3, D4});
  shown.clear(false);
  drawStatic(next);
  drawCounter(next, 0);
  commit(true);
}

void loop() {
  static unsigned n = 0, partials = 0;
  delay(10000);
  drawCounter(next, ++n);
  // Partial refreshes leave faint ghosts over time, and when many black pixels turn white. Do a full
  // refresh every so often, or when the change clears a lot of black.
  bool full = ++partials >= 30 || next.clearedPixels(shown) > 1500;
  if (full) partials = 0;
  commit(full);
}
