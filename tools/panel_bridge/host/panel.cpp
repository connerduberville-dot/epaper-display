// panel.dll: the project's own renderer (layout_v2.h) and EPD driver (epd_qyeg1085.cpp), compiled for Windows.
// The driver's pin and SPI calls are batched into packets for the STM32 bridge on the demo board
// (tools/panel_bridge/stm32/bridge.c). Used from server.py through ctypes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "shim/Arduino.h"
#include "shim/SPI.h"
#include "../../../firmware/SeptaBoard/config.h"
#include "../../../firmware/SeptaBoard/layout_v2.h"
#include "../../../firmware/SeptaBoard/epd_qyeg1085.cpp"
#define EXPORT extern "C" __declspec(dllexport)
using namespace septa;

// ---------------------------------------------------------------- bridge transport
SPIClass SPI;
static HANDLE g_port = INVALID_HANDLE_VALUE;
static std::vector<uint8_t> g_ops;
static const size_t NONE = (size_t)-1;
static size_t g_spiOp = NONE;              // index of the open 0x02 op's length field
static uint8_t g_pins = 0x0F, g_sent = 0xFF;
static int g_busy = 1;
static char g_err[160] = "";
static const size_t MAXOPS = 4000;
enum { PIN_CS = 1, PIN_CS2 = 0, PIN_DC = 2, PIN_RST = 3, PIN_BUSY = 4 };

static void fail(const char* m) { if (!g_err[0]) snprintf(g_err, sizeof g_err, "%s", m); }

static bool flushOps() {
  if (g_err[0] || g_port == INVALID_HANDLE_VALUE) { g_ops.clear(); g_spiOp = NONE; return false; }
  uint8_t x = 0; for (uint8_t v : g_ops) x ^= v;
  std::vector<uint8_t> pkt; pkt.reserve(g_ops.size() + 4);
  pkt.push_back(0xA5); pkt.push_back((uint8_t)(g_ops.size() & 0xFF)); pkt.push_back((uint8_t)(g_ops.size() >> 8));
  pkt.insert(pkt.end(), g_ops.begin(), g_ops.end()); pkt.push_back(x);
  g_ops.clear(); g_spiOp = NONE;
  DWORD w = 0;
  if (!WriteFile(g_port, pkt.data(), (DWORD)pkt.size(), &w, nullptr) || w != pkt.size()) { fail("serial write failed"); return false; }
  uint8_t r[4]; DWORD got = 0, n = 0;
  while (got < 4) {
    if (!ReadFile(g_port, r + got, 4 - got, &n, nullptr) || n == 0) { fail("bridge did not answer (timeout)"); return false; }
    got += n;
  }
  if (r[0] != 0x5A) { fail("bridge reply out of sync"); return false; }
  if (r[1] != 0) { char m[64]; snprintf(m, sizeof m, "bridge error 0x%02X", r[1]); fail(m); return false; }
  g_busy = r[2];
  return true;
}
static void need(size_t n) { if (g_ops.size() + n > MAXOPS) flushOps(); }
static void emitPins() {
  if (g_pins == g_sent) return;
  need(2); g_ops.push_back(0x01); g_ops.push_back(g_pins); g_sent = g_pins; g_spiOp = NONE;
}
void bridge_spi(const uint8_t* b, size_t n) {
  emitPins();
  while (n) {
    if (g_spiOp == NONE || g_ops.size() + 1 > MAXOPS) {
      need(4); g_ops.push_back(0x02); g_spiOp = g_ops.size(); g_ops.push_back(0); g_ops.push_back(0);
    }
    size_t room = MAXOPS - g_ops.size(), k = n < room ? n : room;
    uint16_t cur = (uint16_t)(g_ops[g_spiOp] | (g_ops[g_spiOp + 1] << 8));
    g_ops.insert(g_ops.end(), b, b + k); cur = (uint16_t)(cur + k);
    g_ops[g_spiOp] = cur & 0xFF; g_ops[g_spiOp + 1] = cur >> 8;
    b += k; n -= k;
    if (g_ops.size() >= MAXOPS) flushOps();
  }
}
void pinMode(int, int) {}
void digitalWrite(int pin, int v) {
  int bit = pin == PIN_CS ? 0 : pin == PIN_CS2 ? 1 : pin == PIN_DC ? 2 : pin == PIN_RST ? 3 : -1;
  if (bit < 0) return;
  if (v) g_pins |= (uint8_t)(1 << bit); else g_pins &= (uint8_t)~(1 << bit);
  emitPins();
}
int digitalRead(int) { flushOps(); return g_err[0] ? HIGH : (g_busy ? HIGH : LOW); }   // on error: don't spin on BUSY
unsigned long millis() { return (unsigned long)GetTickCount64(); }
void delay(unsigned long ms) { flushOps(); Sleep(ms); }

EXPORT int pb_open(const char* port) {
  std::string path = std::string("\\\\.\\") + port;
  g_port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (g_port == INVALID_HANDLE_VALUE) { snprintf(g_err, sizeof g_err, "cannot open %s", port); return 0; }
  DCB d = {}; d.DCBlength = sizeof d; GetCommState(g_port, &d);
  d.BaudRate = 921600; d.ByteSize = 8; d.Parity = NOPARITY; d.StopBits = ONESTOPBIT; d.fBinary = 1;
  d.fOutxCtsFlow = 0; d.fOutxDsrFlow = 0; d.fDtrControl = DTR_CONTROL_DISABLE; d.fRtsControl = RTS_CONTROL_DISABLE;
  d.fOutX = 0; d.fInX = 0; d.fParity = 0; d.fAbortOnError = 0;
  SetCommState(g_port, &d);
  COMMTIMEOUTS t = {}; t.ReadTotalTimeoutConstant = 3000; t.WriteTotalTimeoutConstant = 5000;
  SetCommTimeouts(g_port, &t);
  PurgeComm(g_port, PURGE_RXCLEAR | PURGE_TXCLEAR);
  g_err[0] = 0; g_ops.clear(); g_spiOp = NONE; g_sent = 0xFF;
  return flushOps() ? 1 : 0;                   // empty packet = ping
}
EXPORT void pb_close() { if (g_port != INVALID_HANDLE_VALUE) CloseHandle(g_port); g_port = INVALID_HANDLE_VALUE; }
EXPORT const char* pb_error() { return g_err; }
EXPORT void pb_clear_error() { g_err[0] = 0; }
EXPORT int pb_busy_level() { flushOps(); return g_busy; }

// ---------------------------------------------------------------- display policy (same as SeptaBoard.ino paint())
static EpdQyeg1085 epd;
static Framebuffer shown, nextFb;
static bool everPainted = false, epdBegun = false;
static int partialsSinceFull = 0;
static unsigned long lastFullMs = 0;

// info: [0] 0 = nothing to do, 1 = partial, 2 = full; [1] total ms; [2] refresh-wait ms; [3] busy timeout; [4] partials since full
EXPORT int pb_paint(const uint8_t* img, int forceFull, int fullEveryN, int fullMaxAgeMin, int* info) {
  if (!epdBegun) { epd.begin({PIN_CS, PIN_CS2, PIN_DC, PIN_RST, PIN_BUSY}); epdBegun = true; }
  memcpy(nextFb.px, img, FB_BYTES);
  Rect l = nextFb.diffRect(shown, 0, HALF_W), r = nextFb.diffRect(shown, HALF_W, W);
  info[0] = 0; info[4] = partialsSinceFull;
  if (!forceFull && everPainted && l.empty() && r.empty()) return 1;
  bool full = forceFull || !everPainted || partialsSinceFull >= fullEveryN ||
              (millis() - lastFullMs) > (unsigned long)fullMaxAgeMin * 60000UL ||
              nextFb.clearedPixels(shown) > FULL_REFRESH_IF_CLEARED_PX;       // same rule as SeptaBoard.ino
  unsigned long t0 = millis();
  if (full) { epd.fullUpdate(nextFb); partialsSinceFull = 0; lastFullMs = millis(); }
  else      { epd.partialUpdate(shown, nextFb, l, r); ++partialsSinceFull; }
  flushOps();
  if (g_err[0]) return 0;
  memcpy(shown.px, nextFb.px, FB_BYTES);
  everPainted = true;
  info[0] = full ? 2 : 1; info[1] = (int)(millis() - t0); info[2] = (int)epd.lastRefreshMs();
  info[3] = epd.lastBusyTimedOut() ? 1 : 0; info[4] = partialsSinceFull;
  return 1;
}
EXPORT void pb_forget_shown() { everPainted = false; }

// ---------------------------------------------------------------- rendering
static int parseTrains(const char* text, Train* in, int cap) {
  int n = 0; std::string all = text ? text : "";
  size_t pos = 0;
  while (n < cap && pos < all.size()) {
    size_t e = all.find('\n', pos); if (e == std::string::npos) e = all.size();
    std::string line = all.substr(pos, e - pos); pos = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> f; size_t s = 0;
    for (;;) {
      size_t b = line.find('|', s);
      f.push_back(line.substr(s, b == std::string::npos ? std::string::npos : b - s));
      if (b == std::string::npos) break;
      s = b + 1;
    }
    if (f.size() < 3) continue;
    Train& t = in[n];
    snprintf(t.number, sizeof t.number, "%s", f[0].c_str()); snprintf(t.departure, sizeof t.departure, "%s", f[1].c_str());
    snprintf(t.delayRaw, sizeof t.delayRaw, "%s", f[2].c_str());
    t.schedMin = parseClock(t.departure); t.lateMin = parseDelayMinutes(t.delayRaw);
    t.direct = f.size() < 4 || f[3] != "0";
    if (t.schedMin >= 0) ++n;
  }
  return n;
}
// "number|sched|est|status|delayRaw|lateMin|direct|inMin" per line
static void describe(const Train* t, int n, int nowMin, char* out, int cap) {
  std::string s;
  for (int i = 0; i < n; ++i) {
    char sh[8], sap[4], eh[8], eap[4], st[24], line[200];
    fmtClock(t[i].schedMin, sh, sizeof sh, sap, sizeof sap);
    fmtClock(t[i].schedMin + t[i].lateMin, eh, sizeof eh, eap, sizeof eap);
    statusLabel(t[i].delayRaw, st, sizeof st);
    snprintf(line, sizeof line, "%s|%s %s|%s %s|%s|%s|%d|%d|%d\n", t[i].number, sh, sap, eh, eap, st, t[i].delayRaw,
             t[i].lateMin, t[i].direct ? 1 : 0, minutesUntil(t[i], nowMin));
    s += line;
  }
  snprintf(out, cap, "%s", s.c_str());
}
EXPORT int pb_render_board(const char* trains, int nowMin, const char* clock, const char* updated, const char* fault,
                           const char* faultDetail, uint8_t* fbOut, char* shownOut, int cap) {
  static Train in[16]; static Framebuffer fb; BoardData d;
  int n = parseTrains(trains, in, 16);
  d.nowMin = nowMin;
  snprintf(d.clock, sizeof d.clock, "%s", clock); snprintf(d.updated, sizeof d.updated, "%s", updated);
  if (fault && *fault) { d.fault = fault; d.faultDetail = (faultDetail && *faultDetail) ? faultDetail : nullptr; d.n = 0; }
  else d.n = pickUpcoming(in, n, nowMin, d.trains, 3);
  renderV2(fb, d);
  memcpy(fbOut, fb.px, FB_BYTES);
  describe(d.trains, d.n, nowMin, shownOut, cap);
  return d.n;
}
EXPORT int pb_describe(const char* trains, int nowMin, char* out, int cap) {
  static Train in[16]; int n = parseTrains(trains, in, 16); describe(in, n, nowMin, out, cap); return n;
}
// Same test pattern as SeptaBoard.ino paintTestPattern().
EXPORT void pb_render_pattern(uint8_t* fbOut) {
  static Framebuffer p; p.clear(false);
  p.fillRect(0, 0, W, 6, true); p.fillRect(0, H - 6, W, 6, true);
  p.fillRect(0, 0, 6, H, true); p.fillRect(W - 6, 0, 6, H, true);
  p.fillRect(HALF_W - 2, 0, 4, H, true);
  for (int x = 40; x < HALF_W - 40; x += 32) p.fillRect(x, 60, 16, 60, true);
  for (int y = 60; y < 120; y += 16) p.fillRect(HALF_W + 40, y, HALF_W - 80, 8, true);
  p.text(60, 300, "LEFT  (CS)", 3, true);
  p.text(HALF_W + 60, 300, "RIGHT  (CS2)", 3, true);
  p.text(60, 420, "TOP-LEFT CORNER IS HERE", 1, true);
  p.fillRect(HALF_W + 60, 360, 200, 90, true); p.text(HALF_W + 80, 425, "INK", 3, false);
  memcpy(fbOut, p.px, FB_BYTES);
}
