// Sun and moon, computed on the board from latitude/longitude and the clock (no network needed).
// Pure C++ (no Arduino), so the host preview (hosttest/render_display.cpp) uses the same code.
// Accuracy: about a minute for sunrise/sunset (NOAA's simplified solar position), a few hours for moon phase,
// which is plenty for a wall display.
#pragma once
#include <math.h>
#include <time.h>

namespace septa {

namespace astro_detail {
constexpr double PI_ = 3.14159265358979323846, D2R = PI_ / 180.0, R2D = 180.0 / PI_;
}

// Sun elevation above the horizon in degrees at a UTC instant.
inline double sunElevation(double latDeg, double lonDeg, time_t t) {
  using namespace astro_detail;
  double d = (double)t / 86400.0 - 10957.5;                  // days since J2000.0 (2000-01-01 12:00 UTC)
  double g = fmod(357.529 + 0.98560028 * d, 360.0) * D2R;    // mean anomaly
  double q = fmod(280.459 + 0.98564736 * d, 360.0);          // mean longitude
  double L = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;  // ecliptic longitude
  double e = (23.439 - 0.00000036 * d) * D2R;                // obliquity
  double ra = atan2(cos(e) * sin(L), cos(L));
  double dec = asin(sin(e) * sin(L));
  double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);   // hours
  double ha = (gmst * 15.0 + lonDeg) * D2R - ra;             // hour angle
  double lat = latDeg * D2R;
  return asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha)) * R2D;
}

constexpr double SUN_HORIZON = -0.833;   // upper limb touching the horizon, with refraction

// Sunrise and sunset in the 24 hours starting at `dayStart` (a UTC instant, normally local midnight).
// Either may be 0 (polar day/night). Found by scanning in 4-minute steps and refining to the minute.
struct SunDay { time_t rise = 0, set = 0; };
inline SunDay sunDay(double lat, double lon, time_t dayStart) {
  SunDay r;
  double prev = sunElevation(lat, lon, dayStart) - SUN_HORIZON;
  for (int m = 4; m <= 1440; m += 4) {
    double cur = sunElevation(lat, lon, dayStart + m * 60) - SUN_HORIZON;
    if ((prev < 0) != (cur < 0)) {
      time_t lo = dayStart + (m - 4) * 60, hi = dayStart + m * 60;   // bisect to the second
      bool rising = prev < 0;
      while (hi - lo > 20) {
        time_t mid = lo + (hi - lo) / 2;
        bool below = sunElevation(lat, lon, mid) - SUN_HORIZON < 0;
        if (below == rising) lo = mid; else hi = mid;
      }
      if (rising && !r.rise) r.rise = hi;
      if (!rising && !r.set) r.set = hi;
    }
    prev = cur;
  }
  return r;
}

// Moon: age in days since new moon (0..29.53) and illuminated fraction (0..1).
struct MoonPhase { double age, illum; bool waxing; const char* name; };
inline MoonPhase moonPhase(time_t t) {
  using namespace astro_detail;
  const double SYNODIC = 29.530588853;
  double days = ((double)t - 947182440.0) / 86400.0;   // reference new moon 2000-01-06 18:14 UTC
  double age = fmod(days, SYNODIC); if (age < 0) age += SYNODIC;
  MoonPhase m;
  m.age = age; m.illum = (1 - cos(2 * PI_ * age / SYNODIC)) / 2; m.waxing = age < SYNODIC / 2;
  static const char* NAMES[8] = {"NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
                                 "FULL MOON", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"};
  m.name = NAMES[(int)floor(age / SYNODIC * 8 + 0.5) % 8];
  return m;
}

}  // namespace septa
