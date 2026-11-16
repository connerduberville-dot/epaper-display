// Fetches SEPTA NextToArrive over HTTPS and fills Train records. Requires the ArduinoJson library (7.x).
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "rail_logic.h"

#if __has_include(<NetworkClientSecure.h>)
  #include <NetworkClientSecure.h>      // arduino-esp32 core 3.x
  using SecureClient = NetworkClientSecure;
#else
  #include <WiFiClientSecure.h>         // core 2.x
  using SecureClient = WiFiClientSecure;
#endif

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

// Returns number of trains parsed (>=0), or -1 on error with `err` set.
inline int fetchDepartures(Train* out, int maxOut, const char*& err, const char* origin = SEPTA_ORIGIN, const char* dest = SEPTA_DESTINATION) {
  static char errBuf[48];
  err = nullptr;
  if (WiFi.status() != WL_CONNECTED) { err = "WI-FI OFFLINE"; return -1; }

  SecureClient client;
  // SEPTA's departure feed is public, non-sensitive data; the board does not send anything private.
  // Certificate verification is skipped so the firmware never breaks when SEPTA rotates its CA.
  // To pin instead: client.setCACert(<PEM of www3.septa.org's root>) and remove setInsecure().
  client.setInsecure();
  client.setHandshakeTimeout(12);

  HTTPClient http;
  http.setConnectTimeout(10000); http.setTimeout(12000);
  http.useHTTP10(true);
  String url = String("https://www3.septa.org/api/NextToArrive/index.php?req1=") + urlEncode(origin)
             + "&req2=" + urlEncode(dest) + "&req3=" + SEPTA_RESULTS;
  if (!http.begin(client, url)) { err = "HTTPS SETUP ERROR"; return -1; }
  http.addHeader("Cache-Control", "no-cache");
  int code = http.GET();
  if (code != 200) { snprintf(errBuf, sizeof errBuf, "FEED ERROR %d", code); err = errBuf; http.end(); return -1; }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e || !doc.is<JsonArray>()) { err = "INVALID FEED"; return -1; }

  int n = 0;
  for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
    if (n >= maxOut) break;
    if (!v.is<JsonObjectConst>()) continue;
    JsonObjectConst row = v.as<JsonObjectConst>();
    const char* num = row["orig_train"] | "";
    const char* dep = row["orig_departure_time"] | "";
    const char* dly = row["orig_delay"] | "";
    if (!*num || !*dep) continue;
    Train& t = out[n];
    snprintf(t.number, sizeof t.number, "%s", num);
    snprintf(t.departure, sizeof t.departure, "%s", dep);
    snprintf(t.delayRaw, sizeof t.delayRaw, "%s", dly);
    t.schedMin = parseClock(dep);
    t.lateMin = parseDelayMinutes(dly);
    JsonVariantConst d = row["isdirect"];
    t.direct = d.is<bool>() ? d.as<bool>() : containsNoCase(d | "true", "true");
    if (t.schedMin >= 0) ++n;
  }
  return n;
}

}  // namespace septa
