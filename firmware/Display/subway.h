// Subway map screen, for the display hung vertically (portrait, 480 x 1360).
// The map itself is a generated bitmap (subway_map.h, made by tools/subway_map/make_map.py from SEPTA's GTFS
// data); this file unpacks it into the landscape framebuffer with the chosen rotation and draws the clock and the
// status footer in the boxes the map leaves blank. Pure C++, so it also renders on the PC.
//
// Refresh: only the clock changes within an hour (a partial refresh well under the ghost limit).
#pragma once
#include <stdio.h>
#include <time.h>
#include "framebuffer.h"
#include "subway_map.h"

namespace septa {

// Portrait view of the landscape panel. turnLeft = false: hang the display with the panel's LEFT edge at the top
// (turn it 90 degrees clockwise); true: RIGHT edge at the top.
struct Portrait {
  Framebuffer& fb;
  bool turnLeft;
  static constexpr int PW = 480, PH = 1360;
  inline void set(int px, int py, bool black) {
    if (px < 0 || px >= PW || py < 0 || py >= PH) return;
    if (!turnLeft) fb.set(py, H - 1 - px, black); else fb.set(W - 1 - py, px, black);
  }
  void fillRect(int x, int y, int w, int h, bool black) {
    for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) set(xx, yy, black);
  }
  // Same bitmap fonts as Framebuffer::text, drawn upright in portrait coordinates. Returns the advance.
  int text(int x, int y, const char* s, int font, bool black) {
    int x0 = x;
    for (; s && *s; ++s) {
      int c = (unsigned char)*s; if (c >= 'a' && c <= 'z') c -= 32;
      if (c < septa_fonts::FIRST_CODE || c > septa_fonts::LAST_CODE) c = '?';
      septa_fonts::Glyph g; memcpy_P(&g, &septa_fonts::GLYPHS[font][c - septa_fonts::FIRST_CODE], sizeof g);
      int stride = (g.w + 7) / 8;
      const uint8_t* bits = septa_fonts::BITS[font] + g.off;
      for (int yy = 0; yy < g.h; ++yy)
        for (int xx = 0; xx < g.w; ++xx)
          if (pgm_read_byte(bits + yy * stride + (xx >> 3)) & (0x80 >> (xx & 7))) set(x + g.left + xx, y + g.top + yy, black);
      x += g.adv;
    }
    return x - x0;
  }
  int textRight(int xr, int y, const char* s, int font, bool black) { return text(xr - Framebuffer::textWidth(s, font), y, s, font, black); }
#ifndef ARDUINO
  static void memcpy_P(void* d, const void* s, size_t n) { memcpy(d, s, n); }
#endif
};

struct SubwayInfo {
  time_t now = 0;             // 0 = clock not set
  bool turnLeft = false;
  const char* footerLeft = "";
  const char* footerRight = "";
};

inline void renderSubway(Framebuffer& fb, const SubwayInfo& s) {
  using namespace septa_map;
  static_assert(septa_map::PW == Portrait::PW && septa_map::PH == Portrait::PH, "map size");
  fb.clear(false);
  Portrait p{fb, s.turnLeft};
  // Unpack the run-length-encoded map: (count, byte) pairs, 60 bytes per portrait row.
  uint32_t out = 0;
  for (uint32_t i = 0; i + 1 < RLE_BYTES; i += 2) {
    uint8_t n = pgm_read_byte(RLE + i), v = pgm_read_byte(RLE + i + 1);
    for (uint8_t k = 0; k < n; ++k, ++out) {
      if (!v) continue;
      int py = out / (PW / 8), px = (out % (PW / 8)) * 8;
      for (int b = 0; b < 8; ++b) if (v & (0x80 >> b)) p.set(px + b, py, true);
    }
  }
  // Clock, top right.
  static const char* DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char t[16] = "--:--", ap[4] = "", date[24] = "";
  if (s.now > 1700000000) {
    struct tm lt; localtime_r(&s.now, &lt);
    int h = lt.tm_hour % 12; if (!h) h = 12;
    snprintf(t, sizeof t, "%d:%02d", h, lt.tm_min);
    snprintf(ap, sizeof ap, "%s", lt.tm_hour < 12 ? "AM" : "PM");
    snprintf(date, sizeof date, "%s %s %d", DAYS[lt.tm_wday], MONTHS[lt.tm_mon], lt.tm_mday);
  }
  int apw = Framebuffer::textWidth(ap, 1);
  p.textRight(CLOCK_X1 - 4 - apw - 6, CLOCK_Y0 + 44, t, 3, true);   // 54 px digits
  p.text(CLOCK_X1 - 4 - apw, CLOCK_Y0 + 44, ap, 1, true);
  p.textRight(CLOCK_X1 - 4, CLOCK_Y0 + 76, date, 0, true);
  // Footer (BUILD n stays visible so updates can be confirmed at a glance).
  p.text(16, FOOTER_Y + 30, s.footerLeft, 0, true);
  p.textRight(PW - 16, FOOTER_Y + 30, s.footerRight, 0, true);
}

}  // namespace septa
