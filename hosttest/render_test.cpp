// Host test: renders the v2 layout with sample data to PBM, plus rail-logic assertions.
#include <cassert>
#include <cstdio>
#include "../firmware/SeptaBoard/layout_v2.h"
using namespace septa;

static void fillTrain(Train& t, const char* num, const char* dep, const char* delay) {
  snprintf(t.number, sizeof t.number, "%s", num); snprintf(t.departure, sizeof t.departure, "%s", dep);
  snprintf(t.delayRaw, sizeof t.delayRaw, "%s", delay);
  t.schedMin = parseClock(dep); t.lateMin = parseDelayMinutes(delay); t.direct = true;
}
static void savePBM(const Framebuffer& fb, const char* path) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P4\n%d %d\n", W, H); fwrite(fb.px, 1, FB_BYTES, f); fclose(f);
}

int main() {
  // rail_logic assertions
  assert(parseClock("10:03AM") == 603); assert(parseClock("12:03PM") == 723); assert(parseClock("12:15AM") == 15); assert(parseClock("1:03 PM") == 783);
  assert(parseDelayMinutes("34 mins") == 34); assert(parseDelayMinutes("On time") == 0);
  char s[24]; statusLabel("On time", s, sizeof s); assert(!strcmp(s, "ON TIME"));
  statusLabel("2 mins", s, sizeof s); assert(!strcmp(s, "2 MIN LATE"));
  statusLabel("Cancelled", s, sizeof s); assert(!strcmp(s, "CANCELLED"));
  char hh[8], ap[4]; fmtClock(603 + 34, hh, 8, ap, 4); assert(!strcmp(hh, "10:37") && !strcmp(ap, "AM"));

  Train in[4]; fillTrain(in[0], "6583", "7:44AM", "34 mins"); fillTrain(in[1], "5319", "10:03AM", "34 mins");
  fillTrain(in[2], "5323", "11:03AM", "On time"); fillTrain(in[3], "5327", "12:03PM", "2 mins");
  BoardData d; d.nowMin = 10 * 60 + 15; snprintf(d.clock, 12, "10:15 AM"); snprintf(d.updated, 12, "10:15 AM");
  d.n = pickUpcoming(in, 4, d.nowMin, d.trains, 3);
  assert(d.n == 3 && !strcmp(d.trains[0].number, "5319"));   // 7:44+34 = 8:18, already gone
  assert(minutesUntil(d.trains[0], d.nowMin) == 22);

  static Framebuffer fb, fb2;
  renderV2(fb, d); savePBM(fb, "late.pbm");

  // Change-only / dirty-rect behaviour: advance one minute -> only countdown & clock regions differ
  BoardData d2 = d; d2.nowMin++; snprintf(d2.clock, 12, "10:16 AM"); snprintf(d2.updated, 12, "10:16 AM");
  renderV2(fb2, d2);
  Rect left = fb2.diffRect(fb, 0, HALF_W), right = fb2.diffRect(fb, HALF_W, W);
  printf("dirty left: %d,%d %dx%d  right: %d,%d %dx%d\n", left.x, left.y, left.w, left.h, right.x, right.y, right.w, right.h);
  assert(!left.empty() && left.y >= 452);  // only the footer 'UPDATED' time changed on the left
  assert(!right.empty() && right.x % 8 == 0 && right.w % 8 == 0);

  BoardData f; f.fault = "WI-FI SETUP"; f.faultDetail = "JOIN SEPTA-BOARD-SETUP"; snprintf(f.clock, 12, "--:--");
  renderV2(fb, f); savePBM(fb, "setup.pbm");
  puts("all assertions passed");
}
