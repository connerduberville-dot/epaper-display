// SEPTA NextToArrive (Regional Rail departures between two stations) for the train board screen.
// Ported from reference_firmware/SeptaBoard/septa_api.h; this version verifies SEPTA's certificate with the core's
// CA bundle (secureClient) instead of setInsecure(), and parses with a filter so only a few KB of RAM are used.
// parseDepartures() is plain ArduinoJson (host-testable); fetchDepartures() needs the Arduino core.
#pragma once
#include "train_board.h"

#if defined(ARDUINOJSON_VERSION) || defined(ARDUINO)
#include <ArduinoJson.h>
namespace septa {

// Fills up to maxOut trains from a NextToArrive JSON array. Returns the count, or -1 if it isn't a valid feed.
template <typename Input>
inline int parseDepartures(Input&& in, Train* out, int maxOut) {
  JsonDocument filter;
  filter[0]["orig_train"] = true; filter[0]["orig_departure_time"] = true;
  filter[0]["orig_delay"] = true; filter[0]["isdirect"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) || !doc.is<JsonArray>()) return -1;
  int n = 0;
  for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
    if (n >= maxOut) break;
    const char* num = v["orig_train"] | "";
    const char* dep = v["orig_departure_time"] | "";
    const char* dly = v["orig_delay"] | "";
    if (!*num || !*dep) continue;
    Train& t = out[n];
    snprintf(t.number, sizeof t.number, "%s", num);
    snprintf(t.departure, sizeof t.departure, "%s", dep);
    snprintf(t.delayRaw, sizeof t.delayRaw, "%s", dly);
    t.schedMin = parseClock(dep);
    t.lateMin = parseDelayMinutes(dly);
    JsonVariantConst d = v["isdirect"];
    t.direct = d.is<bool>() ? d.as<bool>() : containsNoCase(d | "true", "true");
    if (t.schedMin >= 0) ++n;
  }
  return n;
}

}  // namespace septa
#endif

#ifdef ARDUINO
#include <HTTPClient.h>
#include "ota.h"   // secureClient(), httpGetFollow()
namespace septa {

inline String urlEncode(const char* s) {
  String r;
  for (; *s; ++s) {
    unsigned char c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); r += b; }
  }
  return r;
}

// Returns the number of trains (>= 0), or -1 with `err` set.
inline int fetchDepartures(const char* origin, const char* dest, Train* out, int maxOut, const char*& err) {
  static char errBuf[48];
  err = nullptr;
  if (WiFi.status() != WL_CONNECTED) { err = "WI-FI OFFLINE"; return -1; }
  String url = String("https://www3.septa.org/api/NextToArrive/index.php?req1=") + urlEncode(origin)
             + "&req2=" + urlEncode(dest) + "&req3=" + maxOut;
  SecureClient sc; NetworkClient plain;
  secureClient(sc);
  HTTPClient http;
  http.setConnectTimeout(10000); http.setTimeout(12000); http.useHTTP10(true);
  int code = httpGetFollow(http, sc, plain, url, [](HTTPClient& h) { h.addHeader("Cache-Control", "no-cache"); });
  if (code != 200) { snprintf(errBuf, sizeof errBuf, "FEED ERROR %d", code); err = errBuf; http.end(); return -1; }
  int n = parseDepartures(http.getStream(), out, maxOut);
  http.end();
  if (n < 0) { err = "INVALID FEED"; return -1; }
  return n;
}

}  // namespace septa
#endif
