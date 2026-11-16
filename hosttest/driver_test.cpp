#include <cassert>
#include <cstdio>
#include "mock/Arduino.h"
#include "mock/SPI.h"
int g_pinState[64]; int g_busyReadsLeft = 0; unsigned long g_ms = 0; std::vector<Byte> g_bus; SPIClass SPI;
#include "../firmware/SeptaBoard/epd_qyeg1085.cpp"
using namespace septa;
// Extract the sequence of command bytes (dc=0) with which chip selects were active
static std::string cmds(size_t from = 0) { std::string s; char b[32]; for (size_t i = from; i < g_bus.size(); ++i) if (!g_bus[i].dc) { snprintf(b, 32, "%02X%s%s ", g_bus[i].v, g_bus[i].cs ? "m" : "", g_bus[i].cs2 ? "s" : ""); s += b; } return s; }
int main() {
  EpdQyeg1085 e; e.begin({1, 0, 2, 21, 22});
  static Framebuffer img; img.clear(false); img.fillRect(600, 100, 160, 8, true);  // straddles the chip boundary
  g_bus.clear(); e.fullUpdate(img);
  std::string c = cmds();
  printf("full: %s\n", c.c_str());
  // vendor order: 4D A6 B4 B6 B7 F7 (both), AE (slave), E0 00 06 30 50 60 61 62 E8, then 10/13 master, 10/13 slave, 01(slave) 04 12 02 07
  assert(c.rfind("4Dms A6ms B4ms B6ms B7ms F7ms AEs E0ms 00ms 06ms 30ms 50ms 60ms 61ms 62ms E8ms 10m 13m 10s 13s 01s 04ms 12ms 02ms 07ms ", 0) == 0);
  // data volume: 4 x 40800 bytes of image data
  size_t dataBytes = 0; for (auto& b : g_bus) if (b.dc) ++dataBytes;
  printf("full data bytes: %zu (expect 4*40800 + init params)\n", dataBytes);
  assert(dataBytes >= 4 * 40800);
  // The black bar at x=600..759: master bytes 75..79 of row 100 are 0xFF, slave bytes 0..9 of row 100 are 0xFF
  // Find master 0x13 stream: after cmd 13m, row 100 is bytes 100*85..100*85+84
  size_t i13m = 0; for (size_t i = 0; i < g_bus.size(); ++i) if (!g_bus[i].dc && g_bus[i].v == 0x13 && g_bus[i].cs && !g_bus[i].cs2) { i13m = i + 1; break; }
  assert(g_bus[i13m + 100 * 85 + 75].v == 0xFF && g_bus[i13m + 100 * 85 + 74].v == 0x00 && g_bus[i13m + 100 * 85 + 84].v == 0xFF);
  size_t i13s = 0; for (size_t i = i13m; i < g_bus.size(); ++i) if (!g_bus[i].dc && g_bus[i].v == 0x13 && g_bus[i].cs2 && !g_bus[i].cs) { i13s = i + 1; break; }
  assert(g_bus[i13s + 100 * 85 + 0].v == 0xFF && g_bus[i13s + 100 * 85 + 9].v == 0xFF && g_bus[i13s + 100 * 85 + 10].v == 0x00);

  // Partial: change only right half region
  static Framebuffer img2; memcpy(img2.px, img.px, FB_BYTES); img2.fillRect(1000, 200, 48, 16, true);
  Rect l = img2.diffRect(img, 0, HALF_W), r = img2.diffRect(img, HALF_W, W);
  assert(l.empty() && r.x == 1000 && r.w == 48 && r.y == 200 && r.h == 16);
  g_bus.clear(); e.partialUpdate(img, img2, l, r);
  c = cmds(); printf("partial: %s\n", c.c_str());
  assert(c.find("00ms 06ms 50ms") != std::string::npos);          // no 0x30 in partial init, matches vendor
  assert(c.find("82ms 01ms 2Ams 31ms 32ms 20ms 21ms 22ms 23ms 24ms 91m 90m 10m 13m 91s 90s 10s 13s 01s 04ms 12ms 02ms 07ms") != std::string::npos);
  // slave window params: x local = 1000-680 = 320 -> 0x01,0x40 ; x end 367 -> 0x01,0x6F ; full height: y 0 -> 0x00,0x00 ; y end 479 -> 0x01,0xDF
  // (windows are always full height: shorter ones streak the first column on real glass)
  size_t i90s = 0; for (size_t i = 0; i < g_bus.size(); ++i) if (!g_bus[i].dc && g_bus[i].v == 0x90 && g_bus[i].cs2) { i90s = i + 1; break; }
  uint8_t want[9] = {0x01, 0x40, 0x01, 0x6F, 0x00, 0x00, 0x01, 0xDF, 0x00};
  for (int k = 0; k < 9; ++k) assert(g_bus[i90s + k].v == want[k]);
  // new-data bytes for slave window: 480 rows x 6 bytes; rows 200-215 are 0xFF, the rest white
  size_t i13 = 0; for (size_t i = i90s; i < g_bus.size(); ++i) if (!g_bus[i].dc && g_bus[i].v == 0x13 && g_bus[i].cs2) { i13 = i + 1; break; }
  for (int k = 0; k < 480 * 6; ++k) assert(g_bus[i13 + k].v == ((k / 6 >= 200 && k / 6 < 216) ? 0xFF : 0x00));
  puts("driver command stream matches vendor sequence; window math and chip split verified");
}
