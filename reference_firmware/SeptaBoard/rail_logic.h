// Hardware-independent SEPTA departure logic. Host-testable (no Arduino dependencies).
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

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
