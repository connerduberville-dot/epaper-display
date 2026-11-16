// Shapes and weather icons for 1-bit screens. Pure C++ (also used by the host preview).
#pragma once
#include <math.h>
#include <stdlib.h>
#include "framebuffer.h"
#include "weather.h"

namespace septa {

inline void fillCircle(Framebuffer& fb, int cx, int cy, int r, bool black) {
  for (int y = -r; y <= r; ++y) {
    int dx = (int)sqrtf((float)(r * r - y * y) + 0.25f * r);
    for (int x = -dx; x <= dx; ++x) fb.set(cx + x, cy + y, black);
  }
}

inline void ring(Framebuffer& fb, int cx, int cy, int r, int thick, bool black) {
  float ro = r + 0.5f, ri = r - thick + 0.5f;
  for (int y = -r - 1; y <= r + 1; ++y)
    for (int x = -r - 1; x <= r + 1; ++x) {
      float d = sqrtf((float)(x * x + y * y));
      if (d <= ro && d >= ri) fb.set(cx + x, cy + y, black);
    }
}

// Line with round ends, `w` pixels wide.
inline void thickLine(Framebuffer& fb, float x0, float y0, float x1, float y1, int w, bool black) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  int steps = (int)len + 1, r = w / 2;
  for (int i = 0; i <= steps; ++i) {
    float t = (float)i / steps;
    int x = (int)lroundf(x0 + dx * t), y = (int)lroundf(y0 + dy * t);
    if (r <= 0) fb.set(x, y, black); else fillCircle(fb, x, y, r, black);
  }
}

inline void fillTriangle(Framebuffer& fb, int x0, int y0, int x1, int y1, int x2, int y2, bool black) {
  int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
  auto edge = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
  for (int y = miny; y <= maxy; ++y)
    for (int x = minx; x <= maxx; ++x) {
      int a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y), c = edge(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) fb.set(x, y, black);
    }
}

// Number followed by a degree ring sized to the font. Returns the advance width.
inline int tempText(Framebuffer& fb, int x, int baseline, int value, int font, bool black) {
  static const int CAP[6] = {16, 20, 28, 39, 117, 87};    // digit height per font (fonts.h)
  char s[8]; snprintf(s, sizeof s, "%d", value);
  int adv = fb.text(x, baseline, s, font, black);
  int cap = CAP[font], r = cap / 7 + 2, th = cap / 14 + 2;
  ring(fb, x + adv + r + cap / 20, baseline - cap + r, r, th, black);
  return adv + 2 * r + cap / 10 + 2;
}

// ---------- weather icons, centred on (cx, cy), about `s` pixels across ----------
namespace icon_detail {
// The cloud outline: three circles on a flat base. `in` shrinks it (to cut out the white interior).
inline bool inCloud(float x, float y, float s, float in) {
  auto c = [&](float ox, float oy, float r) { float dx = x - ox * s, dy = y - oy * s, rr = r * s - in; return rr > 0 && dx * dx + dy * dy <= rr * rr; };
  if (c(-0.24f, 0.08f, 0.20f) || c(0.02f, -0.06f, 0.28f) || c(0.27f, 0.10f, 0.18f)) return true;
  return x >= -0.24f * s && x <= 0.27f * s && y >= 0.08f * s && y <= 0.28f * s - in;   // flat base
}
}

// Outlined cloud (black outline `t` px, white inside). dy shifts it down from the icon centre.
inline void drawCloud(Framebuffer& fb, int cx, int cy, int s, int t) {
  int r = s / 2 + 2;
  for (int y = -r; y <= r; ++y)
    for (int x = -r; x <= r; ++x) {
      if (!icon_detail::inCloud((float)x, (float)y, (float)s, 0)) continue;
      bool inner = icon_detail::inCloud((float)x, (float)y, (float)s, (float)t) &&
                   icon_detail::inCloud((float)x - t, (float)y, (float)s, 0) && icon_detail::inCloud((float)x + t, (float)y, (float)s, 0);
      fb.set(cx + x, cy + y, !inner);
    }
}

inline void drawSun(Framebuffer& fb, int cx, int cy, int s) {
  int r = s * 2 / 9, w = s / 16 + 1;
  if (w < 2) w = 2;
  fillCircle(fb, cx, cy, r, true);
  for (int i = 0; i < 8; ++i) {
    float a = i * 3.14159265f / 4, c = cosf(a), d = sinf(a);
    thickLine(fb, cx + c * (r + s * 0.09f), cy + d * (r + s * 0.09f), cx + c * (s * 0.46f), cy + d * (s * 0.46f), w, true);
  }
}

inline void drawMoonIcon(Framebuffer& fb, int cx, int cy, int s) {   // a crescent, for clear nights
  int r = s * 3 / 8;
  fillCircle(fb, cx, cy, r, true);
  fillCircle(fb, cx + r * 3 / 5, cy - r * 2 / 5, r * 4 / 5, false);
}

// Moon as it looks tonight: lit part white with a black outline, dark part black. (Northern-hemisphere view.)
inline void drawMoonPhase(Framebuffer& fb, int cx, int cy, int r, double age) {
  const double SYN = 29.530588853;
  double ph = age / SYN;                       // 0 new, 0.5 full
  double k = cos(2 * 3.14159265358979 * ph);   // terminator x-scale: 1 new, -1 full
  for (int y = -r; y <= r; ++y) {
    float hw = sqrtf((float)(r * r - y * y));
    float term = (float)(k * hw);
    for (int x = -r; x <= r; ++x) {
      if (x * x + y * y > r * r) continue;
      bool lit = ph < 0.5 ? (x > term) : (x < -term);
      fb.set(cx + x, cy + y, !lit);
    }
  }
  ring(fb, cx, cy, r, 2, true);
}

inline void drawWeatherIcon(Framebuffer& fb, int cx, int cy, int s, int code, bool night) {
  Sky k = skyOf(code);
  int t = s / 14 + 1; if (t < 2) t = 2;
  auto sunOrMoon = [&](int x, int y, int sz) { if (night) drawMoonIcon(fb, x, y, sz); else drawSun(fb, x, y, sz); };
  switch (k) {
    case Sky::Clear: sunOrMoon(cx, cy, s); return;
    case Sky::Partly:
      sunOrMoon(cx - s / 6, cy - s / 6, s * 3 / 4);
      drawCloud(fb, cx + s / 10, cy + s / 8, s * 3 / 4, t);
      return;
    case Sky::Cloudy: drawCloud(fb, cx, cy, s, t); return;
    case Sky::Fog:
      for (int i = 0; i < 4; ++i) {
        int y = cy - s * 3 / 10 + i * s / 5, off = (i % 2) ? s / 10 : 0;
        thickLine(fb, cx - s * 0.42f + off, y, cx + s * 0.42f - (s / 10 - off), y, t + 1, true);
      }
      return;
    default: break;
  }
  // Precipitation: a cloud at the top, then rain, drizzle, snow or a bolt underneath.
  drawCloud(fb, cx, cy - s / 7, s * 9 / 10, t);
  int y0 = cy + s * 2 / 9, y1 = cy + s * 7 / 16;
  if (k == Sky::Rain || k == Sky::Drizzle) {
    int n = k == Sky::Rain ? 3 : 3, w = k == Sky::Rain ? t : t - 1 > 1 ? t - 1 : 1;
    for (int i = 0; i < n; ++i) {
      int x = cx - s / 4 + i * s / 4;
      if (k == Sky::Rain) thickLine(fb, x + s / 20, y0, x - s / 14, y1, w + 1, true);
      else { fillCircle(fb, x, y0 + s / 20, w, true); fillCircle(fb, x - s / 20, y1 - s / 20, w, true); }
    }
  } else if (k == Sky::Snow) {
    for (int i = 0; i < 3; ++i) {
      int x = cx - s / 4 + i * s / 4, y = (i % 2) ? y1 - s / 30 : y0 + s / 14, a = s / 13 + 1;
      thickLine(fb, x - a, y, x + a, y, 2, true);
      thickLine(fb, x - a / 2, y - a, x + a / 2, y + a, 2, true);
      thickLine(fb, x + a / 2, y - a, x - a / 2, y + a, 2, true);
    }
  } else {   // thunder
    int bx = cx, by = y0 - s / 20;
    fillTriangle(fb, bx + s / 10, by, bx - s / 9, by + s / 7, bx + s / 40, by + s / 7, true);
    fillTriangle(fb, bx - s / 40, by + s / 8, bx + s / 9, by + s / 8, bx - s / 12, by + s * 3 / 10, true);
  }
}

}  // namespace septa
