// How many black pixels turn white on each minute tick, per clock font (ghost rule threshold is 1500).
#include <cstdio>
#include <initializer_list>
#include "../firmware/Display/framebuffer.h"
using namespace septa;
int main() {
  static Framebuffer a, b;
  for (int font : {2, 3}) {
    unsigned worst = 0, over = 0, total = 0; char worstAt[16] = "";
    for (int m = 0; m < 24 * 60; ++m) {
      int n = (m + 1) % 1440;
      if (n % 60 == 0) continue;            // hour changes: allowed to be a full refresh
      char s1[8], s2[8];
      int h1 = (m / 60) % 12 ? (m / 60) % 12 : 12, h2 = (n / 60) % 12 ? (n / 60) % 12 : 12;
      snprintf(s1, 8, "%d:%02d", h1, m % 60); snprintf(s2, 8, "%d:%02d", h2, n % 60);
      a.clear(false); b.clear(false);
      a.text((W - Framebuffer::textWidth(s1, font)) / 2, 300, s1, font, true);
      b.text((W - Framebuffer::textWidth(s2, font)) / 2, 300, s2, font, true);
      unsigned c = b.clearedPixels(a); ++total;
      if (c > 1500) ++over;
      if (c > worst) { worst = c; snprintf(worstAt, 16, "%s->%s", s1, s2); }
    }
    printf("font %d: worst %u px (%s), %u of %u ticks over 1500\n", font, worst, worstAt, over, total);
  }
}
