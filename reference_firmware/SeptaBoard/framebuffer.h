// 1-bit framebuffer for the 1360x480 panel. 1 = black. MSB = leftmost pixel in each byte.
// Row stride 170 bytes: bytes 0..84 belong to the left controller (CS), 85..169 to the right (CS2).
#pragma once
#include <stdint.h>
#include <string.h>
#include "fonts.h"

namespace septa {

constexpr int W = 1360, H = 480, STRIDE = W / 8;      // 170
constexpr int HALF_W = 680, HALF_STRIDE = HALF_W / 8;   // 85
constexpr size_t FB_BYTES = (size_t)STRIDE * H;        // 81,600

struct Rect { int x, y, w, h; bool empty() const { return w <= 0 || h <= 0; } };

class Framebuffer {
 public:
  uint8_t px[FB_BYTES];
  void clear(bool black = false) { memset(px, black ? 0xFF : 0x00, FB_BYTES); }
  inline void set(int x, int y, bool black) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t& b = px[y * STRIDE + (x >> 3)];
    uint8_t m = 0x80 >> (x & 7);
    if (black) b |= m; else b &= ~m;
  }
  inline bool get(int x, int y) const {
    if (x < 0 || x >= W || y < 0 || y >= H) return false;
    return px[y * STRIDE + (x >> 3)] & (0x80 >> (x & 7));
  }
  void fillRect(int x, int y, int w, int h, bool black) {
    for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) set(xx, yy, black);
  }
  // Draw uppercase-only bitmap text at baseline (x,y). Returns advance width.
  int text(int x, int y, const char* s, int font, bool black) {
    if (!s) return 0;
    int x0 = x;
    for (; *s; ++s) {
      int c = toUpperAscii((unsigned char)*s);
      if (c < septa_fonts::FIRST_CODE || c > septa_fonts::LAST_CODE) c = '?';
      septa_fonts::Glyph g; memcpy_P(&g, &septa_fonts::GLYPHS[font][c - septa_fonts::FIRST_CODE], sizeof g);
      int stride = (g.w + 7) / 8;
      const uint8_t* bits = septa_fonts::BITS[font] + g.off;
      for (int yy = 0; yy < g.h; ++yy)
        for (int xx = 0; xx < g.w; ++xx)
          if (pgm_read_byte(bits + yy * stride + (xx >> 3)) & (0x80 >> (xx & 7)))
            set(x + g.left + xx, y + g.top + yy, black);
      x += g.adv;
    }
    return x - x0;
  }
  static int textWidth(const char* s, int font) {
    int w = 0;
    for (; s && *s; ++s) {
      int c = toUpperAscii((unsigned char)*s);
      if (c < septa_fonts::FIRST_CODE || c > septa_fonts::LAST_CODE) c = '?';
      septa_fonts::Glyph g; memcpy_P(&g, &septa_fonts::GLYPHS[font][c - septa_fonts::FIRST_CODE], sizeof g);
      w += g.adv;
    }
    return w;
  }
  // Pixels that are black in `before` and white here. Partial refresh leaves a faint ghost where large
  // black areas turn white, so the display policy switches to a full refresh above a threshold.
  uint32_t clearedPixels(const Framebuffer& before) const {
    uint32_t n = 0;
    for (size_t i = 0; i < FB_BYTES; ++i) n += (uint32_t)__builtin_popcount((unsigned)(before.px[i] & (uint8_t)~px[i]));
    return n;
  }
  // Bounding box of pixels that differ from `other`, restricted to columns [cx0,cx1). Byte-aligned in x.
  Rect diffRect(const Framebuffer& other, int cx0 = 0, int cx1 = W) const {
    int bx0 = cx0 / 8, bx1 = cx1 / 8;
    int minx = STRIDE, maxx = -1, miny = H, maxy = -1;
    for (int y = 0; y < H; ++y) {
      const uint8_t* a = px + y * STRIDE; const uint8_t* b = other.px + y * STRIDE;
      for (int bx = bx0; bx < bx1; ++bx) if (a[bx] != b[bx]) {
        if (bx < minx) minx = bx;
        if (bx > maxx) maxx = bx;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
      }
    }
    if (maxx < 0) return {0, 0, 0, 0};
    return {minx * 8, miny, (maxx - minx + 1) * 8, maxy - miny + 1};
  }
 private:
  static inline int toUpperAscii(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
#ifndef ARDUINO
  static void memcpy_P(void* d, const void* s, size_t n) { memcpy(d, s, n); }
#endif
};

}  // namespace septa
