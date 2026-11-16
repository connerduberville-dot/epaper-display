// The home screen ("sky dashboard"): greeting, clock, current weather, and a 24-hour panorama across the
// right two thirds with night shaded from the real sun position, the temperature curve, chance of rain,
// sunrise/sunset, the moon tonight and how the daylight is changing.
//
// Pure drawing code: also renders on the PC (hosttest/render_display.cpp) for previews without the panel.
//
// Refresh behaviour (CLAUDE.md section 3): within an hour only the clock, the "now" marker and small text
// change, all partial updates well under the 1,500-px ghost limit. The panorama starts at the current hour,
// so it shifts once an hour, together with the new forecast; that change is large and the display policy
// turns it into the hourly full refresh it would do anyway.
#pragma once
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "framebuffer.h"
#include "draw.h"
#include <initializer_list>
#include "astro.h"
#include "weather.h"

namespace septa {

struct DashInfo {
  time_t now = 0;                     // 0 = clock not set yet
  double lat = 0, lon = 0;
  const char* name = "";              // for the greeting; empty = no name
  const char* place = "";             // shown above the panorama, e.g. "PHILADELPHIA"
  const Weather* wx = nullptr;        // nullptr or !ok = no forecast (sun and moon still work)
  const char* wxStatus = "";          // why there is no forecast
  const char* footerLeft = "";
  const char* footerRight = "";
  const char* notice = nullptr;       // problem line, e.g. content URL failed
};

namespace dash {
constexpr int LX = 40;                      // left panel
constexpr int CX0 = 486, COL = 34, HOURS = 24, CX1 = CX0 + COL * HOURS;   // panorama 486..1302
constexpr int ICON_Y = 92, LABEL_Y = 146;   // forecast icons and temperatures
constexpr int BAND_T = 160, BAND_B = 302;   // temperature band (night shaded)
constexpr int RAIN_T = 306, RAIN_B = 346;   // chance-of-rain bars
constexpr int HOUR_Y = 374;                 // hour labels

inline void hourLabel(char* out, size_t cap, int hour24) {
  int h = hour24 % 12; if (!h) h = 12;
  snprintf(out, cap, "%d %s", h, hour24 < 12 ? "AM" : "PM");
}
inline void clockText(char* out, size_t cap, const struct tm& t) {
  int h = t.tm_hour % 12; if (!h) h = 12;
  snprintf(out, cap, "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
}
inline time_t localMidnight(time_t now) {
  struct tm t; localtime_r(&now, &t);
  t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
  return mktime(&t);
}
inline int roundi(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
// Night: 25% dots; twilight: 1-in-8.
inline bool shade(int x, int y, double elev) {
  if (elev >= SUN_HORIZON) return false;
  if (elev < -6) return (x % 2 == 0) && (y % 2 == 0);
  return (x % 4 == 0) && (y % 2 == 0) && ((x / 4 + y / 2) % 2 == 0);
}
}  // namespace dash

inline void drawCenteredText(Framebuffer& fb, int cx, int baseline, const char* s, int font, bool black) {
  fb.text(cx - Framebuffer::textWidth(s, font) / 2, baseline, s, font, black);
}

inline void renderDashboard(Framebuffer& fb, const DashInfo& d) {
  using namespace dash;
  fb.clear(false);
  const Weather* wx = (d.wx && d.wx->ok) ? d.wx : nullptr;
  bool haveTime = d.now > 1700000000;
  struct tm lt = {};
  if (haveTime) localtime_r(&d.now, &lt);
  char buf[96];

  // ---------------- left: greeting, clock, date ----------------
  static const char* DAYS[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  static const char* MONTHS[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
                                 "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  if (haveTime) {
    const char* part = lt.tm_hour < 5 ? "GOOD NIGHT" : lt.tm_hour < 12 ? "GOOD MORNING" : lt.tm_hour < 17 ? "GOOD AFTERNOON" : "GOOD EVENING";
    if (d.name && *d.name) snprintf(buf, sizeof buf, "%s, %s", part, d.name); else snprintf(buf, sizeof buf, "%s", part);
    fb.text(LX, 50, buf, 1, true);
    clockText(buf, sizeof buf, lt);
    fb.text(LX - 2, 112, buf, 3, true);   // 54 px: minute ticks stay under the ghost limit (hosttest/clock_ticks.cpp)
    snprintf(buf, sizeof buf, "%s, %s %d", DAYS[lt.tm_wday], MONTHS[lt.tm_mon], lt.tm_mday);
    fb.text(LX, 150, buf, 1, true);
  } else {
    fb.text(LX, 50, "HELLO", 1, true);
    fb.text(LX - 2, 112, "--:--", 3, true);
    fb.text(LX, 150, "WAITING FOR NETWORK TIME", 1, true);
  }
  fb.fillRect(LX, 172, 400, 2, true);

  // ---------------- left: weather now ----------------
  double elevNow = haveTime ? sunElevation(d.lat, d.lon, d.now) : 10;
  bool nightNow = elevNow < SUN_HORIZON;
  if (wx) {
    drawWeatherIcon(fb, LX + 58, 238, 112, wx->code, nightNow);
    tempText(fb, LX + 134, 284, roundi(wx->temp), 5, true);
    fb.text(LX, 322, conditionText(wx->code, nightNow), 1, true);
    snprintf(buf, sizeof buf, "FEELS %d  HI %d  LO %d  WIND %d %s", roundi(wx->feels), roundi(wx->hi), roundi(wx->lo),
             roundi(wx->wind), wx->metric ? "KM/H" : "MPH");
    fb.text(LX, 352, buf, 0, true);
  } else {
    fb.text(LX, 230, "NO FORECAST YET", 2, true);
    fb.text(LX, 270, d.wxStatus && *d.wxStatus ? d.wxStatus : "SET YOUR LOCATION IN SETTINGS", 0, true);
  }

  // ---------------- left: moon and daylight ----------------
  // Sun events for yesterday, today and tomorrow. (Soft-float trig on the C6: compute each day once.)
  time_t mid = haveTime ? localMidnight(d.now) : 0;
  SunDay today, yday, tmrw;
  if (haveTime) { today = sunDay(d.lat, d.lon, mid); yday = sunDay(d.lat, d.lon, mid - 86400); tmrw = sunDay(d.lat, d.lon, mid + 86400); }
  if (haveTime) {
    MoonPhase mp = moonPhase(d.now);
    drawMoonPhase(fb, LX + 18, 384, 17, mp.age);
    snprintf(buf, sizeof buf, "%s, %d%% LIT", mp.name, (int)(mp.illum * 100 + 0.5));
    fb.text(LX + 48, 377, buf, 0, true);
    if (today.rise && today.set && yday.rise && yday.set) {
      long len = (long)(today.set - today.rise), dl = len - (long)(yday.set - yday.rise);
      long adl = dl < 0 ? -dl : dl;
      snprintf(buf, sizeof buf, "%ldH %02ldM DAYLIGHT, %c%ldM %02ldS", len / 3600, (len / 60) % 60, dl < 0 ? '-' : '+', adl / 60, adl % 60);
      fb.text(LX + 48, 402, buf, 0, true);
    }
  }

  // ---------------- right: 24-hour panorama ----------------
  fb.fillRect(CX0 - 26, 20, 2, 380, true);   // divider
  if (haveTime) {
    struct tm h0 = lt; h0.tm_min = 0; h0.tm_sec = 0; h0.tm_isdst = -1;
    time_t start = mktime(&h0);
    auto xAt = [&](time_t t) { return CX0 + (int)((double)(t - start) * COL / 3600.0); };

    // Title, and sunrise/sunset of the next ones to come.
    const SunDay& a = today; const SunDay& b = tmrw;
    time_t rise = a.rise > d.now ? a.rise : b.rise, set = a.set > d.now ? a.set : b.set;
    char rs[48] = "", r1[16] = "", s1[16] = "";
    struct tm tt;
    if (rise) { localtime_r(&rise, &tt); clockText(r1, sizeof r1, tt); }
    if (set) { localtime_r(&set, &tt); clockText(s1, sizeof s1, tt); }
    snprintf(rs, sizeof rs, "SUNRISE %s   SUNSET %s", rise ? r1 : "--", set ? s1 : "--");
    fb.text(CX1 - Framebuffer::textWidth(rs, 0), 44, rs, 0, true);

    // Rain outlook in the title.
    const char* title = "NEXT 24 HOURS";
    int i0 = -1;
    if (wx) for (int i = 0; i < wx->n; ++i) if (wx->t[i] == start) { i0 = i; break; }
    char tbuf[64];
    if (wx && i0 >= 0) {
      int first = -1, peak = 0;
      for (int k = 0; k < HOURS && i0 + k < wx->n; ++k) {
        if (first < 0 && wx->pop[i0 + k] >= 50) first = k;
        if (wx->pop[i0 + k] > peak) peak = wx->pop[i0 + k];
      }
      if (first == 0) snprintf(tbuf, sizeof tbuf, "%s NOW", skyOf(wx->codes[i0]) == Sky::Snow ? "SNOW" : "RAIN LIKELY");
      else if (first > 0) {
        char hl[12]; struct tm ft; time_t tf = wx->t[i0 + first]; localtime_r(&tf, &ft); hourLabel(hl, sizeof hl, ft.tm_hour);
        snprintf(tbuf, sizeof tbuf, "%s LIKELY BY %s", skyOf(wx->codes[i0 + first]) == Sky::Snow ? "SNOW" : "RAIN", hl);
      } else if (peak >= 20) snprintf(tbuf, sizeof tbuf, "SMALL CHANCE OF RAIN");
      else snprintf(tbuf, sizeof tbuf, "DRY FOR 24 HOURS");
      title = tbuf;
    }
    fb.text(CX0, 46, title, 1, true);

    // Night and twilight shading behind the curve and the rain bars, from the computed sun height per column.
    for (int x = CX0; x < CX1; ++x) {
      double e = sunElevation(d.lat, d.lon, start + (time_t)((x - CX0) * 3600.0 / COL));
      for (int y = BAND_T; y < RAIN_B; ++y) if (shade(x, y, e)) fb.set(x, y, true);
    }
    // Sunrise / sunset ticks across the band, with a little sun at the top.
    for (time_t ev : {a.rise, a.set, b.rise, b.set}) {
      if (!ev || ev < start || ev >= start + HOURS * 3600) continue;
      int x = xAt(ev);
      for (int y = BAND_T; y < RAIN_B; y += 6) fb.fillRect(x, y, 2, 3, true);
    }
    fb.fillRect(CX0, RAIN_B, CX1 - CX0, 2, true);   // baseline under the rain bars

    // Hour ticks and labels every 3 hours, with an icon and temperature above.
    for (int k = 0; k <= HOURS; ++k) {
      time_t t = start + k * 3600;
      struct tm ht; localtime_r(&t, &ht);
      int x = CX0 + k * COL;
      fb.fillRect(x, RAIN_B + 2, 1, k % 3 == 0 || ht.tm_hour % 3 == 0 ? 6 : 3, true);
      if (ht.tm_hour % 3) continue;
      char hl[12]; hourLabel(hl, sizeof hl, ht.tm_hour);
      if (ht.tm_hour == 0) snprintf(hl, sizeof hl, "%s", DAYS[ht.tm_wday] + 0), hl[3] = 0;   // midnight: day name
      int lx = x - Framebuffer::textWidth(hl, 0) / 2;
      if (lx < CX0 - 20) lx = CX0 - 20;
      if (lx + Framebuffer::textWidth(hl, 0) > W - 8) lx = W - 8 - Framebuffer::textWidth(hl, 0);
      fb.text(lx, HOUR_Y, hl, 0, true);
      if (wx && i0 >= 0 && i0 + k < wx->n && k < HOURS) {
        bool n = sunElevation(d.lat, d.lon, t + 1800) < SUN_HORIZON;
        drawWeatherIcon(fb, x, ICON_Y, 46, wx->codes[i0 + k], n);
        char tv[8]; snprintf(tv, sizeof tv, "%d", roundi(wx->temps[i0 + k]));
        int tw = Framebuffer::textWidth(tv, 0) + 8;
        tempText(fb, x - tw / 2, LABEL_Y, roundi(wx->temps[i0 + k]), 0, true);
      }
    }

    if (wx && i0 >= 0) {
      int last = i0 + HOURS < wx->n ? HOURS : wx->n - 1 - i0;   // hours available after start
      // Temperature curve, scaled to this window's range.
      float mn = 1e9f, mx = -1e9f;
      for (int k = 0; k <= last; ++k) { float v = wx->temps[i0 + k]; if (v < mn) mn = v; if (v > mx) mx = v; }
      if (mx - mn < 8) { float c = (mx + mn) / 2; mn = c - 4; mx = c + 4; }
      auto yOf = [&](float v) { return BAND_B - 14 - (v - mn) / (mx - mn) * (BAND_B - BAND_T - 34); };
      for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < last; ++k) {
          float xa = CX0 + k * COL, xb = xa + COL;
          // Smooth with a cosine blend between hourly points.
          float va = wx->temps[i0 + k], vb = wx->temps[i0 + k + 1], px = xa, py = yOf(va);
          for (int s = 1; s <= 6; ++s) {
            float u = s / 6.0f, m = (1 - cosf(u * 3.14159265f)) / 2;
            float qx = xa + (xb - xa) * u, qy = yOf(va + (vb - va) * m);
            if (pass == 0) thickLine(fb, px, py, qx, qy, 9, false);   // white halo over the night dots
            else thickLine(fb, px, py, qx, qy, 4, true);
            px = qx; py = qy;
          }
        }
      // Chance of rain: a bar per hour, height = probability.
      for (int k = 0; k < last; ++k) {
        int p = wx->pop[i0 + k];
        if (p < 5) continue;
        int h = (RAIN_B - RAIN_T) * p / 100;
        fb.fillRect(CX0 + k * COL + 5, RAIN_B - h, COL - 10, h, true);
      }
    } else {
      const char* msg = wx ? "FORECAST IS OUT OF DATE" : "FORECAST NOT AVAILABLE";
      int mw = Framebuffer::textWidth(msg, 1) + 40, my = (BAND_T + BAND_B) / 2;
      fb.fillRect((CX0 + CX1 - mw) / 2, my - 26, mw, 44, false);
      drawCenteredText(fb, (CX0 + CX1) / 2, my + 8, msg, 1, true);
    }

    // Now marker: a small triangle above the band, and a dotted line through it.
    int nx = xAt(d.now);
    fillTriangle(fb, nx - 8, BAND_T - 12, nx + 8, BAND_T - 12, nx, BAND_T - 1, true);
    for (int y = BAND_T; y < RAIN_B; y += 4) fb.fillRect(nx, y, 2, 2, true);
  }

  // ---------------- footer ----------------
  if (d.notice && *d.notice) drawCenteredText(fb, W / 2, 404, d.notice, 0, true);
  fb.fillRect(40, 418, W - 80, 2, true);
  fb.text(40, 456, d.footerLeft, 0, true);
  fb.text(W - 40 - Framebuffer::textWidth(d.footerRight, 0), 456, d.footerRight, 0, true);
  if (d.place && *d.place) drawCenteredText(fb, W / 2, 456, d.place, 0, true);
}

}  // namespace septa
