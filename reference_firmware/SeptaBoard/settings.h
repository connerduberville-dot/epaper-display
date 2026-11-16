// Runtime settings, editable from the web UI and persisted in flash (Preferences namespace "cfg").
// Defaults come from config.h. Anything not stored falls back to the default.
#pragma once
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>
#include "config.h"

namespace septa {

struct Settings {
  // Polling / refresh
  uint16_t pollSec       = POLL_INTERVAL_MS / 1000;
  uint8_t  fullEveryN    = FULL_REFRESH_EVERY_N;
  uint16_t fullMaxAgeMin = FULL_REFRESH_MAX_AGE_MS / 60000UL;
  uint16_t staleMin      = FEED_STALE_AFTER_MS / 60000UL;
  // Night mode: slower polling between nightStart and nightEnd (hours, local). Equal = disabled.
  uint8_t  nightStart    = 1;
  uint8_t  nightEnd      = 5;
  uint16_t nightPollSec  = 600;
  // Route
  char origin[32];
  char dest[32];
  // Power
  bool wifiSleep = true;    // modem sleep between beacons (DTIM). Saves ~50 mA, adds a few ms latency.
  bool cpu80     = false;   // run the CPU at 80 MHz instead of 160.
  // Developer
  bool devMode   = false;

  Settings() { strlcpy(origin, SEPTA_ORIGIN, sizeof origin); strlcpy(dest, SEPTA_DESTINATION, sizeof dest); }

  void load() {
    Preferences p; p.begin("cfg", true);
    pollSec = p.getUShort("poll", pollSec);  fullEveryN = p.getUChar("fulln", fullEveryN);
    fullMaxAgeMin = p.getUShort("fullage", fullMaxAgeMin); staleMin = p.getUShort("stale", staleMin);
    nightStart = p.getUChar("nstart", nightStart); nightEnd = p.getUChar("nend", nightEnd); nightPollSec = p.getUShort("npoll", nightPollSec);
    String o = p.getString("origin", origin), d = p.getString("dest", dest);
    strlcpy(origin, o.c_str(), sizeof origin); strlcpy(dest, d.c_str(), sizeof dest);
    wifiSleep = p.getBool("wsleep", wifiSleep); cpu80 = p.getBool("cpu80", cpu80); devMode = p.getBool("dev", devMode);
    p.end(); clamp();
  }
  void save() const {
    Preferences p; p.begin("cfg", false);
    p.putUShort("poll", pollSec); p.putUChar("fulln", fullEveryN); p.putUShort("fullage", fullMaxAgeMin); p.putUShort("stale", staleMin);
    p.putUChar("nstart", nightStart); p.putUChar("nend", nightEnd); p.putUShort("npoll", nightPollSec);
    p.putString("origin", origin); p.putString("dest", dest);
    p.putBool("wsleep", wifiSleep); p.putBool("cpu80", cpu80); p.putBool("dev", devMode);
    p.end();
  }
  static void reset() { Preferences p; p.begin("cfg", false); p.clear(); p.end(); }

  void clamp() {
    if (pollSec < 15) pollSec = 15;           if (pollSec > 3600) pollSec = 3600;
    if (fullEveryN < 1) fullEveryN = 1;
    if (fullMaxAgeMin < 5) fullMaxAgeMin = 5; if (fullMaxAgeMin > 1440) fullMaxAgeMin = 1440;
    if (staleMin < 2) staleMin = 2;           if (staleMin > 240) staleMin = 240;
    if (nightStart > 23) nightStart = 23;     if (nightEnd > 23) nightEnd = 23;
    if (nightPollSec < 60) nightPollSec = 60; if (nightPollSec > 3600) nightPollSec = 3600;
    if (!origin[0]) strlcpy(origin, SEPTA_ORIGIN, sizeof origin);
    if (!dest[0]) strlcpy(dest, SEPTA_DESTINATION, sizeof dest);
  }

  bool isNight(int hour) const {
    if (nightStart == nightEnd) return false;
    return nightStart < nightEnd ? (hour >= nightStart && hour < nightEnd) : (hour >= nightStart || hour < nightEnd);
  }

  void toJson(JsonObject o) const {
    o["pollSec"] = pollSec; o["fullEveryN"] = fullEveryN; o["fullMaxAgeMin"] = fullMaxAgeMin; o["staleMin"] = staleMin;
    o["nightStart"] = nightStart; o["nightEnd"] = nightEnd; o["nightPollSec"] = nightPollSec;
    o["origin"] = origin; o["dest"] = dest;
    o["wifiSleep"] = wifiSleep; o["cpu80"] = cpu80; o["devMode"] = devMode;
  }
  void fromJson(JsonObjectConst o) {
    pollSec = o["pollSec"] | pollSec; fullEveryN = o["fullEveryN"] | fullEveryN;
    fullMaxAgeMin = o["fullMaxAgeMin"] | fullMaxAgeMin; staleMin = o["staleMin"] | staleMin;
    nightStart = o["nightStart"] | nightStart; nightEnd = o["nightEnd"] | nightEnd; nightPollSec = o["nightPollSec"] | nightPollSec;
    if (o["origin"].is<const char*>()) strlcpy(origin, o["origin"], sizeof origin);
    if (o["dest"].is<const char*>()) strlcpy(dest, o["dest"], sizeof dest);
    wifiSleep = o["wifiSleep"] | wifiSleep; cpu80 = o["cpu80"] | cpu80; devMode = o["devMode"] | devMode;
    clamp();
  }
};

}  // namespace septa
