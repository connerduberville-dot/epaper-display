// Host tool: renders board frames with the firmware's own layout code (layout_v2.h).
// Usage: render_frames <trains.txt> <startMin> <count> <updated "9:05 PM"> <outdir>
// trains.txt: one train per line, "number|departure|delay|direct(1/0)" as SEPTA sends them.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../firmware/SeptaBoard/layout_v2.h"
using namespace septa;

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "usage: render_frames trains.txt startMin count updated outdir\n"); return 2; }
  Train in[16]; int n = 0; char line[256];
  FILE* f = fopen(argv[1], "r"); if (!f) { perror(argv[1]); return 1; }
  while (n < 16 && fgets(line, sizeof line, f)) {
    char* parts[4] = {0}; int p = 0; char* s = line;
    line[strcspn(line, "\r\n")] = 0;
    while (p < 4) { parts[p++] = s; char* bar = strchr(s, '|'); if (!bar) break; *bar = 0; s = bar + 1; }
    if (p < 3) continue;
    Train& t = in[n];
    snprintf(t.number, sizeof t.number, "%s", parts[0]); snprintf(t.departure, sizeof t.departure, "%s", parts[1]);
    snprintf(t.delayRaw, sizeof t.delayRaw, "%s", parts[2]);
    t.schedMin = parseClock(t.departure); t.lateMin = parseDelayMinutes(t.delayRaw);
    t.direct = p < 4 || parts[3][0] != '0';
    if (t.schedMin >= 0) ++n;
  }
  fclose(f);
  int start = atoi(argv[2]), count = atoi(argv[3]);
  static Framebuffer fb;
  for (int k = 0; k < count; ++k) {
    BoardData d; d.nowMin = (start + k) % 1440;
    char hh[8], ap[4]; fmtClock(d.nowMin, hh, sizeof hh, ap, sizeof ap);
    snprintf(d.clock, sizeof d.clock, "%s %s", hh, ap);
    snprintf(d.updated, sizeof d.updated, "%s", argv[4]);
    d.n = pickUpcoming(in, n, d.nowMin, d.trains, 3);
    renderV2(fb, d);
    std::string path = std::string(argv[5]) + "/frame" + std::to_string(k) + ".bin";
    FILE* o = fopen(path.c_str(), "wb"); fwrite(fb.px, 1, FB_BYTES, o); fclose(o);
    printf("frame %d: %s, %d trains shown\n", k, d.clock, d.n);
  }
  return 0;
}
