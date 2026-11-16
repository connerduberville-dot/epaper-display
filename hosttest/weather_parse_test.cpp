// Parses an Open-Meteo response the way the board does (weather.h parseWeather), with no hardware.
// Needs the single-header ArduinoJson 7.3.1 (github.com/bblanchon/ArduinoJson/releases) in an include dir:
//   python -m ziglang c++ -std=c++17 -I<dir with ArduinoJson.h> weather_parse_test.cpp -o wpt.exe && wpt.exe
#include <ArduinoJson.h>
#include <cstdio>
#include <string>
#include "../firmware/Display/weather.h"
using namespace septa;

int main() {
  // Shape of a real response for the URL in buildWeatherUrl() (timeformat=unixtime), trimmed to 26 hours,
  // with the nulls Open-Meteo sends for precipitation_probability past some models' range.
  std::string j = R"({"latitude":39.95,"longitude":-75.16,"generationtime_ms":0.2,"utc_offset_seconds":-18000,
    "timezone":"America/New_York","timezone_abbreviation":"GMT-5","elevation":14.0,
    "current_units":{"time":"unixtime","interval":"seconds","temperature_2m":"°F"},
    "current":{"time":1795316400,"interval":900,"temperature_2m":47.4,"apparent_temperature":43.1,
      "relative_humidity_2m":61,"weather_code":2,"wind_speed_10m":9.3},
    "hourly_units":{"time":"unixtime"},"hourly":{"time":[)";
  long t0 = 1795237200;   // 2026-11-16 00:00 EST
  std::string temps, pops, codes;
  for (int i = 0; i < 26; ++i) {
    j += std::to_string(t0 + i * 3600) + (i < 25 ? "," : "");
    temps += std::to_string(40 + i % 10) + ".5" + (i < 25 ? "," : "");
    pops += (i >= 24 ? std::string("null") : std::to_string(i * 3)) + (i < 25 ? "," : "");
    codes += std::to_string(i < 12 ? 1 : 61) + (i < 25 ? "," : "");
  }
  j += "],\"temperature_2m\":[" + temps + "],\"precipitation_probability\":[" + pops + "],\"weather_code\":[" + codes + "]},";
  j += R"("daily_units":{},"daily":{"time":[1795237200],"temperature_2m_max":[54.1],"temperature_2m_min":[38.6]}})";

  static Weather w;
  const char* e = parseWeather(j.c_str(), w);
  int fails = 0;
  auto check = [&](bool ok, const char* what) { if (!ok) { printf("FAIL: %s\n", what); ++fails; } };
  check(e == nullptr, e ? e : "parse");
  check(w.ok && w.n == 26, "26 hours");
  check(w.t[0] == t0 && w.t[25] == t0 + 25 * 3600, "unix times");
  check(w.temp > 47.3f && w.temp < 47.5f && w.code == 2 && w.humidity == 61, "current");
  check(w.hi > 54 && w.lo < 39, "daily hi/lo");
  check(w.pop[10] == 30 && w.pop[24] == 0, "pop, null -> 0");
  check(w.codes[0] == 1 && w.codes[20] == 61, "codes");

  static Weather bad;
  check(parseWeather("{\"error\":true,\"reason\":\"Latitude must be in range\"}", bad) != nullptr && !bad.ok, "API error");
  check(parseWeather("<html>", bad) != nullptr, "not JSON");

  char url[400]; buildWeatherUrl(url, sizeof url, 39.9526, -75.1652, false);
  printf("%s\n", url);
  printf(fails ? "%d FAILED\n" : "all weather parse checks passed\n", fails);
  return fails ? 1 : 0;
}
