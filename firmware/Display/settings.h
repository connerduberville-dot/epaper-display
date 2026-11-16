// Runtime settings, editable from the web UI and stored in flash (Preferences namespace "disp"; the old train board used "cfg").
// Anything not stored falls back to the default in config.h. The admin password is write-only: the API
// never returns it, only whether one is set.
#pragma once
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>
#include "config.h"

namespace septa {

struct Settings {
  char     updateUrl[200];
  uint16_t checkMin      = DEFAULT_CHECK_MIN;
  bool     autoUpdate    = true;
  char     contentUrl[200];
  uint16_t contentSec    = DEFAULT_CONTENT_SEC;
  char     tz[48];
  uint8_t  fullEveryN    = FULL_REFRESH_EVERY_N;
  uint16_t fullMaxAgeMin = FULL_REFRESH_MAX_AGE_MIN;
  bool     wifiSleep     = true;
  char     adminPass[33];   // empty = no password on the LAN endpoints that change things
  char     name[24];        // home screen greeting
  float    lat           = DEFAULT_LAT;
  float    lon           = DEFAULT_LON;
  bool     weather       = DEFAULT_WEATHER;
  bool     metric        = DEFAULT_METRIC;
  uint8_t  screen        = DEFAULT_SCREEN;   // 0 home screen, 1 subway map
  bool     turnLeft      = DEFAULT_TURN_LEFT;

  Settings() {
    strlcpy(updateUrl, DEFAULT_UPDATE_URL, sizeof updateUrl);
    strlcpy(contentUrl, DEFAULT_CONTENT_URL, sizeof contentUrl);
    strlcpy(tz, DEFAULT_TZ, sizeof tz);
    adminPass[0] = 0;
    strlcpy(name, DEFAULT_NAME, sizeof name);
  }

  void load() {
    Preferences p; p.begin("disp", true);
    getStr(p, "upd", updateUrl, sizeof updateUrl);
    checkMin = p.getUShort("chk", checkMin); autoUpdate = p.getBool("auto", autoUpdate);
    getStr(p, "cnt", contentUrl, sizeof contentUrl); contentSec = p.getUShort("cnts", contentSec);
    getStr(p, "tz", tz, sizeof tz);
    fullEveryN = p.getUChar("fulln", fullEveryN); fullMaxAgeMin = p.getUShort("fullage", fullMaxAgeMin);
    wifiSleep = p.getBool("wsleep", wifiSleep);
    getStr(p, "apw", adminPass, sizeof adminPass);
    getStr(p, "name", name, sizeof name);
    lat = p.getFloat("lat", lat); lon = p.getFloat("lon", lon);
    weather = p.getBool("wx", weather); metric = p.getBool("metric", metric);
    screen = p.getUChar("screen", screen); turnLeft = p.getBool("tleft", turnLeft);
    p.end(); clamp();
  }
  void save() const {
    Preferences p; p.begin("disp", false);
    p.putString("upd", updateUrl); p.putUShort("chk", checkMin); p.putBool("auto", autoUpdate);
    p.putString("cnt", contentUrl); p.putUShort("cnts", contentSec); p.putString("tz", tz);
    p.putUChar("fulln", fullEveryN); p.putUShort("fullage", fullMaxAgeMin); p.putBool("wsleep", wifiSleep);
    p.putString("apw", adminPass);
    p.putString("name", name); p.putFloat("lat", lat); p.putFloat("lon", lon);
    p.putBool("wx", weather); p.putBool("metric", metric);
    p.putUChar("screen", screen); p.putBool("tleft", turnLeft);
    p.end();
  }
  static void reset() { Preferences p; p.begin("disp", false); p.clear(); p.end(); }

  void clamp() {
    if (checkMin < 1) checkMin = 1;            if (checkMin > 1440) checkMin = 1440;
    if (contentSec < 30) contentSec = 30;      if (contentSec > 43200) contentSec = 43200;
    if (fullEveryN < 1) fullEveryN = 1;
    if (fullMaxAgeMin < 5) fullMaxAgeMin = 5;  if (fullMaxAgeMin > 1440) fullMaxAgeMin = 1440;
    if (!tz[0]) strlcpy(tz, DEFAULT_TZ, sizeof tz);
    if (!(lat >= -90 && lat <= 90)) lat = DEFAULT_LAT;      // also catches NaN
    if (!(lon >= -180 && lon <= 180)) lon = DEFAULT_LON;
    if (screen > 1) screen = 0;
  }

  void toJson(JsonObject o) const {
    o["updateUrl"] = updateUrl; o["checkMin"] = checkMin; o["autoUpdate"] = autoUpdate;
    o["contentUrl"] = contentUrl; o["contentSec"] = contentSec; o["tz"] = tz;
    o["fullEveryN"] = fullEveryN; o["fullMaxAgeMin"] = fullMaxAgeMin; o["wifiSleep"] = wifiSleep;
    o["hasAdminPass"] = adminPass[0] != 0;
    o["name"] = name; o["lat"] = lat; o["lon"] = lon; o["weather"] = weather; o["metric"] = metric;
    o["screen"] = screen; o["turnLeft"] = turnLeft ? 1 : 0;
  }
  void fromJson(JsonObjectConst o) {
    if (o["updateUrl"].is<const char*>()) strlcpy(updateUrl, o["updateUrl"], sizeof updateUrl);
    checkMin = o["checkMin"] | checkMin; autoUpdate = o["autoUpdate"] | autoUpdate;
    if (o["contentUrl"].is<const char*>()) strlcpy(contentUrl, o["contentUrl"], sizeof contentUrl);
    contentSec = o["contentSec"] | contentSec;
    if (o["tz"].is<const char*>()) strlcpy(tz, o["tz"], sizeof tz);
    fullEveryN = o["fullEveryN"] | fullEveryN; fullMaxAgeMin = o["fullMaxAgeMin"] | fullMaxAgeMin;
    wifiSleep = o["wifiSleep"] | wifiSleep;
    // Only change the password when the field is present; "" clears it.
    if (o["adminPass"].is<const char*>()) strlcpy(adminPass, o["adminPass"], sizeof adminPass);
    if (o["name"].is<const char*>()) strlcpy(name, o["name"], sizeof name);
    lat = o["lat"] | lat; lon = o["lon"] | lon;
    weather = o["weather"] | weather; metric = o["metric"] | metric;
    screen = num(o["screen"], screen); turnLeft = num(o["turnLeft"], turnLeft ? 1 : 0) != 0;
    clamp();
  }

 private:
  // A form <select> sends its value as a string ("1"); accept numbers and numeric strings.
  static int num(JsonVariantConst v, int dflt) {
    if (v.is<int>()) return v.as<int>();
    if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
    if (v.is<const char*>()) return atoi(v.as<const char*>());
    return dflt;
  }
  static void getStr(Preferences& p, const char* key, char* out, size_t cap) {
    if (!p.isKey(key)) return;                     // keep the default; avoids NVS "not found" log noise
    String s = p.getString(key, out); strlcpy(out, s.c_str(), cap);
  }
};

}  // namespace septa
