// Host preview of firmware/Display's screens. Writes PBM files; convert with Pillow.
//   python -m ziglang c++ -std=c++17 render_display.cpp -o render_display.exe && render_display.exe
//   (Linux/macOS: g++ -std=c++17 -O2 render_display.cpp -o render_display && ./render_display)
// The sky dashboard uses a made-up forecast for Philadelphia on Nov 16, 2026, in US Eastern time.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "../firmware/Display/screen.h"
#include "../firmware/Display/dashboard.h"
using namespace septa;

static void savePBM(const Framebuffer& fb, const char* path) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P4\n%d %d\n", W, H); fwrite(fb.px, 1, FB_BYTES, f); fclose(f);
}

static void setTZ(const char* tz) {
#ifdef _WIN32
  char b[80]; snprintf(b, sizeof b, "TZ=%s", tz); _putenv(b);
#else
  setenv("TZ", tz, 1);
#endif
  tzset();
}

// A plausible November forecast: cool night, mild afternoon, rain moving in tomorrow afternoon.
static void fakeWeather(Weather& w, time_t midnight) {
  w.ok = true; w.n = 72; w.temp = 47.4f; w.feels = 43.2f; w.humidity = 61; w.wind = 9.3f; w.code = 2;
  w.hi = 54.1f; w.lo = 38.6f;
  for (int i = 0; i < w.n; ++i) {
    int h = i % 24;
    w.t[i] = midnight + i * 3600;
    w.temps[i] = 46 + 8 * sinf((h - 9) * 3.14159f / 12) - (i >= 24 ? 2.5f : 0) + (i >= 40 ? -3 : 0);
    w.pop[i] = (i >= 34 && i <= 44) ? (uint8_t)(20 + (i - 34) * 9 > 85 ? 85 : 20 + (i - 34) * 9) : (i >= 30 ? 10 : 0);
    w.codes[i] = i < 26 ? (h < 8 || h > 17 ? 0 : (h < 12 ? 1 : 2)) : i < 34 ? 3 : i < 38 ? 61 : 63;
  }
}

int main() {
  static Framebuffer a, b;
  setTZ("EST5EDT,M3.2.0,M11.1.0");

  // ---- sky dashboard (the home screen) ----
  struct tm t = {}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 16; t.tm_hour = 21; t.tm_min = 41; t.tm_isdst = -1;
  time_t now = mktime(&t);
  struct tm m0 = t; m0.tm_hour = 0; m0.tm_min = 0; time_t midnight = mktime(&m0);
  static Weather w; fakeWeather(w, midnight);
  DashInfo d; d.now = now; d.lat = 39.9526; d.lon = -75.1652; d.name = "CONNER"; d.place = "";
  d.wx = &w; d.footerLeft = "FIRMWARE 1.1.0  BUILD 4"; d.footerRight = "10.0.0.4   EPAPER.LOCAL   UP TO DATE";
  renderDashboard(a, d); savePBM(a, "dash_evening.pbm");

  // Minute ticks within the hour: all must be partial (cleared px under the 1,500 ghost limit).
  unsigned worst = 0; int worstMin = 0;
  for (int hr = 0; hr < 24; ++hr)
    for (int mi = 0; mi < 59; ++mi) {
      struct tm x = t; x.tm_hour = hr; x.tm_min = mi; x.tm_isdst = -1; d.now = mktime(&x); renderDashboard(a, d);
      x.tm_min = mi + 1; x.tm_isdst = -1; d.now = mktime(&x); renderDashboard(b, d);
      unsigned c = b.clearedPixels(a);
      if (c > worst) { worst = c; worstMin = hr * 100 + mi; }
    }
  printf("dashboard minute ticks: worst cleared %u px (at %04d), limit 1500\n", worst, worstMin);

  t.tm_hour = 7; t.tm_min = 5; t.tm_isdst = -1; d.now = mktime(&t); renderDashboard(a, d); savePBM(a, "dash_morning.pbm");
  t.tm_hour = 13; t.tm_min = 20; t.tm_isdst = -1; d.now = mktime(&t); renderDashboard(a, d); savePBM(a, "dash_afternoon.pbm");
  d.wx = nullptr; d.wxStatus = "WEATHER: NO CONNECTION (-1)"; renderDashboard(a, d); savePBM(a, "dash_noweather.pbm");

  // ---- the older built-in screens ----
  ScreenInfo s; s.time = "12:48"; s.ampm = "PM"; s.date = "WEDNESDAY, SEPTEMBER 30";
  s.footerLeft = "FIRMWARE 1.0.0  BUILD 12"; s.footerRight = "10.0.0.4   EPAPER.LOCAL   UP TO DATE";
  drawBanner(a, "INSTALLING UPDATE 1.0.0+13..."); savePBM(a, "display_banner.pbm");
  renderMessage(b, "WI-FI SETUP", "JOIN THE WI-FI NETWORK EPAPER-SETUP ON YOUR PHONE"); savePBM(b, "display_setup.pbm");
  return 0;
}
