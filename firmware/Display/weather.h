// Weather data for the home screen. The struct and text helpers are pure C++ (the host preview uses them);
// parseWeather() needs ArduinoJson, fetchWeather() needs the Arduino core.
//
// Source: Open-Meteo (https://open-meteo.com), free for non-commercial use and needs no API key, so nothing
// secret is involved. Location and units come from Settings.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace septa {

struct Weather {
  static constexpr int MAX_HOURS = 72;
  bool ok = false;
  bool metric = false;               // true: deg C and km/h; false: deg F and mph
  time_t fetched = 0;                // when it was downloaded (epoch), 0 = never
  float temp = 0, feels = 0, wind = 0;
  int humidity = 0, code = 0;        // code: WMO weather code (0 clear ... 99 thunderstorm with hail)
  float hi = 0, lo = 0;              // today's high and low
  int n = 0;                         // hourly points, starting at local midnight today
  time_t t[MAX_HOURS];               // start of each hour, epoch
  float temps[MAX_HOURS];
  uint8_t pop[MAX_HOURS];            // chance of precipitation, %
  uint8_t codes[MAX_HOURS];
};

// What kind of icon a WMO code gets.
enum class Sky : uint8_t { Clear, Partly, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };
inline Sky skyOf(int code) {
  if (code <= 1) return Sky::Clear;
  if (code == 2) return Sky::Partly;
  if (code == 3) return Sky::Cloudy;
  if (code == 45 || code == 48) return Sky::Fog;
  if (code >= 51 && code <= 57) return Sky::Drizzle;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return Sky::Rain;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Thunder;
  return Sky::Cloudy;
}

inline const char* conditionText(int code, bool night) {
  switch (code) {
    case 0: return night ? "CLEAR" : "SUNNY";
    case 1: return night ? "MOSTLY CLEAR" : "MOSTLY SUNNY";
    case 2: return "PARTLY CLOUDY";
    case 3: return "CLOUDY";
    case 45: case 48: return "FOG";
    case 51: case 53: case 55: return "DRIZZLE";
    case 56: case 57: return "FREEZING DRIZZLE";
    case 61: return "LIGHT RAIN";
    case 63: return "RAIN";
    case 65: return "HEAVY RAIN";
    case 66: case 67: return "FREEZING RAIN";
    case 71: return "LIGHT SNOW";
    case 73: return "SNOW";
    case 75: return "HEAVY SNOW";
    case 77: return "SNOW GRAINS";
    case 80: case 81: return "SHOWERS";
    case 82: return "HEAVY SHOWERS";
    case 85: case 86: return "SNOW SHOWERS";
    case 95: return "THUNDERSTORMS";
    case 96: case 99: return "STORMS WITH HAIL";
    default: return "";
  }
}

inline void buildWeatherUrl(char* out, size_t cap, double lat, double lon, bool metric) {
  snprintf(out, cap,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
           "&hourly=temperature_2m,precipitation_probability,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_days=3&timeformat=unixtime%s",
           lat, lon, metric ? "" : "&temperature_unit=fahrenheit&wind_speed_unit=mph");
}

}  // namespace septa

#if defined(ARDUINOJSON_VERSION) || defined(ARDUINO)
#include <ArduinoJson.h>
namespace septa {

// Fill `w` from an Open-Meteo response (the URL above). Returns nullptr or a short error.
template <typename Input>
inline const char* parseWeather(Input&& in, Weather& w) {
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true; filter["current"]["apparent_temperature"] = true;
  filter["current"]["relative_humidity_2m"] = true; filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["hourly"]["time"] = true; filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["precipitation_probability"] = true; filter["hourly"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true; filter["daily"]["temperature_2m_min"] = true;
  filter["error"] = true; filter["reason"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, in, DeserializationOption::Filter(filter));
  if (e) return "weather: bad JSON";
  if (doc["error"] | false) return "weather: API error";
  JsonObjectConst cur = doc["current"], hr = doc["hourly"], dy = doc["daily"];
  if (cur.isNull() || hr.isNull()) return "weather: missing data";
  w.temp = cur["temperature_2m"] | 0.0f; w.feels = cur["apparent_temperature"] | w.temp;
  w.humidity = cur["relative_humidity_2m"] | 0; w.code = cur["weather_code"] | 0;
  w.wind = cur["wind_speed_10m"] | 0.0f;
  w.hi = dy["temperature_2m_max"][0] | w.temp; w.lo = dy["temperature_2m_min"][0] | w.temp;
  JsonArrayConst ts = hr["time"], tp = hr["temperature_2m"], pp = hr["precipitation_probability"], wc = hr["weather_code"];
  int n = 0;
  for (size_t i = 0; i < ts.size() && n < Weather::MAX_HOURS; ++i) {
    w.t[n] = (time_t)(ts[i] | 0LL);
    w.temps[n] = tp[i] | (n ? w.temps[n - 1] : w.temp);
    w.pop[n] = (uint8_t)(pp[i] | 0);       // null for some hours/models: treat as 0
    w.codes[n] = (uint8_t)(wc[i] | 0);
    ++n;
  }
  if (n < 24) return "weather: too few hours";
  w.n = n; w.ok = true;
  return nullptr;
}

}  // namespace septa
#endif

#ifdef ARDUINO
#include <HTTPClient.h>
#include "ota.h"   // secureClient(), httpGetFollow()
namespace septa {

// Download and parse the forecast. Streams the JSON (filtered, ~3 KB in RAM); no frame-sized buffers.
inline const char* fetchWeather(double lat, double lon, bool metric, Weather& out) {
  static char err[48];
  if (WiFi.status() != WL_CONNECTED) return "weather: wi-fi offline";
  char url[400]; buildWeatherUrl(url, sizeof url, lat, lon, metric);
  SecureClient sc; NetworkClient plain;
  secureClient(sc);
  HTTPClient http;
  http.setConnectTimeout(10000); http.setTimeout(15000); http.useHTTP10(true);
  int code = httpGetFollow(http, sc, plain, url, [](HTTPClient&) {});
  if (code < 0) { snprintf(err, sizeof err, "weather: no connection (%d)", code); return err; }
  if (code != 200) { snprintf(err, sizeof err, "weather: HTTP %d", code); http.end(); return err; }
  static Weather w;   // ~1 KB: keep it off the loop task's stack
  w = Weather(); w.metric = metric;
  const char* e = parseWeather(http.getStream(), w);
  http.end();
  if (e) return e;
  w.fetched = time(nullptr);
  out = w;
  return nullptr;
}

}  // namespace septa
#endif
