// Fort Washington departure board: the train board from reference_firmware/SeptaBoard (v1.1.0), as a screen of
// this firmware (so OTA updates and rollback keep working). rail_logic + the v2 "station signage" layout, unchanged
// except: the footer's right corner shows the BUILD number, and the clock string comes from the caller.
// Pure C++ (no Arduino), so it renders on the PC too.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "framebuffer.h"

namespace septa {

struct Train {
  char number[8];      // "5319"
  char departure[10];  // scheduled, "10:03AM" as SEPTA sends it
  char delayRaw[24];   // "On time", "34 mins", "Cancelled"
  int schedMin = -1;   // minutes since midnight
  int lateMin = 0;     // parsed delay minutes
  bool direct = true;
};

// "10:03AM" / "1:03 PM" -> minutes since midnight, or -1.
inline int parseClock(const char* t) {
  if (!t) return -1;
  int h = 0, m = 0; char ap[3] = {0, 0, 0};
  if (sscanf(t, "%d:%d%2s", &h, &m, ap) < 3) { if (sscanf(t, "%d:%d %2s", &h, &m, ap) < 3) return -1; }
  if (h < 1 || h > 12 || m < 0 || m > 59) return -1;
  h %= 12;
  if (toupper(ap[0]) == 'P') h += 12;
  return h * 60 + m;
}

inline int parseDelayMinutes(const char* s) {
  if (!s) return 0;
  for (const char* p = s; *p; ++p) if (isdigit((unsigned char)*p)) return atoi(p);
  return 0;
}

inline bool containsNoCase(const char* hay, const char* needle) {
  size_t n = strlen(needle);
  for (; *hay; ++hay) { size_t i = 0; while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) ++i; if (i == n) return true; }
  return false;
}

// "ON TIME" / "34 MIN LATE" / "CANCELLED" / "SUSPENDED" / "--"
inline void statusLabel(const char* delayRaw, char* out, size_t n) {
  if (!delayRaw || !*delayRaw) { snprintf(out, n, "--"); return; }
  if (containsNoCase(delayRaw, "on time")) { snprintf(out, n, "ON TIME"); return; }
  if (containsNoCase(delayRaw, "cancel")) { snprintf(out, n, "CANCELLED"); return; }
  if (containsNoCase(delayRaw, "suspend")) { snprintf(out, n, "SUSPENDED"); return; }
  int mins = parseDelayMinutes(delayRaw);
  if (mins > 0) { snprintf(out, n, "%d MIN LATE", mins); return; }
  size_t i = 0; for (; delayRaw[i] && i + 1 < n; ++i) out[i] = toupper((unsigned char)delayRaw[i]); out[i] = 0;
}

// Minutes since midnight -> "10:37" and "AM"
inline void fmtClock(int min, char* hhmm, size_t n1, char* ap, size_t n2) {
  min = ((min % 1440) + 1440) % 1440;
  int h = min / 60, m = min % 60;
  snprintf(hhmm, n1, "%d:%02d", ((h + 11) % 12) + 1, m);
  snprintf(ap, n2, "%s", h < 12 ? "AM" : "PM");
}

// Chooses the next three departures that have not yet left (estimated = scheduled + delay).
// nowMin: minutes since local midnight. Returns count written to `out`.
inline int pickUpcoming(const Train* in, int nIn, int nowMin, Train* out, int maxOut) {
  int n = 0;
  for (int i = 0; i < nIn && n < maxOut; ++i) {
    const Train& t = in[i];
    if (!t.direct || t.schedMin < 0) continue;
    int est = t.schedMin + t.lateMin;
    int diff = est - nowMin;
    if (diff < -720) diff += 1440;       // after-midnight wrap
    if (diff < -1) continue;             // already departed
    out[n++] = t;
  }
  return n;
}

inline int minutesUntil(const Train& t, int nowMin) {
  int diff = t.schedMin + t.lateMin - nowMin;
  if (diff < -720) diff += 1440;
  return diff;
}

}  // namespace septa

namespace septa {

struct BoardData {
  // Up to three upcoming trains (already filtered by pickUpcoming), count in `n`.
  Train trains[3]; int n = 0;
  int nowMin = 0;                 // minutes since local midnight
  char clock[12] = "--:--";       // "10:15 AM"
  char updated[12] = "";          // last successful fetch, same format
  const char* fault = nullptr;    // nullptr = healthy; else e.g. "WI-FI OFFLINE"
  const char* faultDetail = nullptr; // optional second line, e.g. AP name during setup
  const char* footerRight = "SEPTA"; // this firmware puts "BUILD n" here
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
  fb.text(rx(0, d.footerRight, R), 474, d.footerRight, 0, true);
}

}  // namespace septa
