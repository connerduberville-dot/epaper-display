// v2 "station signage" layout. Pixel coordinates identical to Live_Board/board_v2_template.html.
#pragma once
#include "framebuffer.h"
#include "rail_logic.h"

namespace septa {

struct BoardData {
  // Up to three upcoming trains (already filtered by pickUpcoming), count in `n`.
  Train trains[3]; int n = 0;
  int nowMin = 0;                 // minutes since local midnight
  char clock[12] = "--:--";       // "10:15 AM"
  char updated[12] = "";          // last successful fetch, same format
  const char* fault = nullptr;    // nullptr = healthy; else e.g. "WI-FI OFFLINE"
  const char* faultDetail = nullptr; // optional second line, e.g. AP name during setup
};

constexpr const char* LINE_NAME = "LANSDALE / DOYLESTOWN LINE";

inline int rx(int font, const char* s, int right) { return right - Framebuffer::textWidth(s, font); }

inline void renderV2(Framebuffer& fb, const BoardData& d) {
  const int L = 40, R = 1320;
  fb.clear(false);

  // Header band (inverted)
  fb.fillRect(0, 0, W, 104, true);
  fb.text(L, 74, "FORT WASHINGTON", 3, false);
  fb.text(rx(0, "REGIONAL RAIL", R), 40, "REGIONAL RAIL", 0, false);
  fb.text(rx(1, d.clock, R), 82, d.clock, 1, false);

  // Route line
  fb.text(L, 146, "TO CENTER CITY", 1, true);
  fb.text(rx(0, LINE_NAME, R), 146, LINE_NAME, 0, true);
  fb.fillRect(L, 162, R - L, 3, true);

  // Strings
  char t0[8] = "--", ap0[4] = "", label0[64] = "", train0[64] = "", pill[24] = "--", countdown[48] = "";
  char t1[8] = "--", ap1[4] = "", n1[8] = "--", st1[24] = "--";
  char t2[8] = "--", ap2[4] = "", n2[8] = "--", st2[24] = "--";

  if (d.fault) {
    snprintf(train0, sizeof train0, "%s", d.fault);
    snprintf(pill, sizeof pill, "NO DATA");
    snprintf(countdown, sizeof countdown, "%s", d.faultDetail ? d.faultDetail : "CHECK SEPTA BEFORE TRAVELING");
  } else if (d.n == 0) {
    snprintf(train0, sizeof train0, "NO DIRECT TRAINS");
    snprintf(pill, sizeof pill, "NO SERVICE");
    snprintf(countdown, sizeof countdown, "CHECK SEPTA FOR SERVICE");
  } else {
    const Train& a = d.trains[0];
    fmtClock(a.schedMin + a.lateMin, t0, sizeof t0, ap0, sizeof ap0);
    char sh[8], sap[4]; fmtClock(a.schedMin, sh, sizeof sh, sap, sizeof sap);
    if (a.lateMin > 0) snprintf(label0, sizeof label0, "ESTIMATED DEPARTURE  /  SCHEDULED %s %s", sh, sap);
    else snprintf(label0, sizeof label0, "SCHEDULED DEPARTURE");
    snprintf(train0, sizeof train0, "TRAIN %s  /  TO SUBURBAN STATION", a.number);
    statusLabel(a.delayRaw, pill, sizeof pill);
    int diff = minutesUntil(a, d.nowMin);
    if (diff <= 0) snprintf(countdown, sizeof countdown, "DEPARTING NOW");
    else if (diff == 1) snprintf(countdown, sizeof countdown, "DEPARTS IN 1 MIN");
    else snprintf(countdown, sizeof countdown, "DEPARTS IN %d MIN", diff);
    if (d.n > 1) { const Train& b = d.trains[1]; fmtClock(b.schedMin + b.lateMin, t1, sizeof t1, ap1, sizeof ap1); snprintf(n1, sizeof n1, "%s", b.number); statusLabel(b.delayRaw, st1, sizeof st1); }
    if (d.n > 2) { const Train& c = d.trains[2]; fmtClock(c.schedMin + c.lateMin, t2, sizeof t2, ap2, sizeof ap2); snprintf(n2, sizeof n2, "%s", c.number); statusLabel(c.delayRaw, st2, sizeof st2); }
  }

  // Next train, left
  fb.text(L, 204, "NEXT TRAIN", 0, true);
  int heroFont = strlen(t0) > 4 ? 5 : 4;
  fb.text(L - 4, 346, t0, heroFont, true);
  fb.text(L - 4 + Framebuffer::textWidth(t0, heroFont) + 16, 346, ap0, 3, true);
  fb.text(L, 392, label0, 0, true);
  fb.text(L, 428, train0, 1, true);

  // Status pill + countdown, right
  int pillW = Framebuffer::textWidth(pill, 3) + 56;
  fb.fillRect(800, 196, pillW, 66, true);
  fb.text(828, 243, pill, 3, false);
  fb.text(800, 312, countdown, 1, true);

  // Following trains table
  fb.fillRect(800, 336, R - 800, 2, true);
  fb.text(800, 362, "THEN", 0, true); fb.text(1010, 362, "TRAIN", 0, true); fb.text(1150, 362, "STATUS", 0, true);
  fb.text(800, 400, t1, 2, true); fb.text(800 + Framebuffer::textWidth(t1, 2) + 8, 400, ap1, 0, true); fb.text(1010, 398, n1, 1, true); fb.text(1150, 398, st1, 1, true);
  fb.text(800, 440, t2, 2, true); fb.text(800 + Framebuffer::textWidth(t2, 2) + 8, 440, ap2, 0, true); fb.text(1010, 438, n2, 1, true); fb.text(1150, 438, st2, 1, true);

  // Footer
  fb.fillRect(L, 452, R - L, 2, true);
  char footer[64];
  if (d.updated[0]) snprintf(footer, sizeof footer, "UPDATED %s  /  LIVE SEPTA DATA", d.updated);
  else snprintf(footer, sizeof footer, "WAITING FOR SEPTA DATA");
  fb.text(L, 474, footer, 0, true);
  fb.text(rx(0, "SEPTA", R), 474, "SEPTA", 0, true);
}

}  // namespace septa
