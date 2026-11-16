// Host preview of firmware/Display's built-in screens. Writes PBM files; convert with Pillow.
//   python -m ziglang c++ -std=c++17 render_display.cpp -o render_display.exe && render_display.exe
#include <cstdio>
#include "../firmware/Display/screen.h"
using namespace septa;

static void savePBM(const Framebuffer& fb, const char* path) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P4\n%d %d\n", W, H); fwrite(fb.px, 1, FB_BYTES, f); fclose(f);
}

int main() {
  static Framebuffer a, b;
  ScreenInfo s; s.time = "12:48"; s.ampm = "PM"; s.date = "WEDNESDAY, SEPTEMBER 30";
  s.hint = "OPEN HTTP://EPAPER.LOCAL ON YOUR PHONE TO SEND A PICTURE OR UPDATE";
  s.footerLeft = "FIRMWARE 1.0.0  BUILD 12"; s.footerRight = "10.0.0.4   EPAPER.LOCAL   UP TO DATE";
  renderScreen(a, s); savePBM(a, "display_clock.pbm");

  // One minute later: how much changes, and would the ghost rule force a full refresh?
  s.time = "12:49"; renderScreen(b, s);
  Rect l = b.diffRect(a, 0, HALF_W), r = b.diffRect(a, HALF_W, W);
  printf("minute tick: left %dx%d right %dx%d, cleared %u px\n", l.w, l.h, r.w, r.h, b.clearedPixels(a));

  s.notice = "CONTENT: CONTENT HTTP 404"; renderScreen(b, s); savePBM(b, "display_notice.pbm");
  drawBanner(a, "INSTALLING UPDATE 1.0.0+13..."); savePBM(a, "display_banner.pbm");
  renderMessage(b, "WI-FI SETUP", "JOIN THE WI-FI NETWORK EPAPER-SETUP ON YOUR PHONE"); savePBM(b, "display_setup.pbm");
  return 0;
}
