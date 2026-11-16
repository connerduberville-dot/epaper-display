// Optional remote content: fetch a raw 1-bit frame (exactly 81,600 bytes, the layout in CLAUDE.md section 2)
// from a URL and stream it straight into a framebuffer, with no third buffer. http:// is allowed for a server on
// your LAN; https:// is verified against the core's CA bundle.
// Make frames on a PC with tools/make_frame.py (any image -> dithered 1-bit .bin).
#pragma once
#include <HTTPClient.h>
#include "framebuffer.h"
#include "ota.h"   // secureClient()

namespace septa {

// Returns nullptr on success, or a short error. `etag` (in/out, may be empty) skips unchanged frames:
// on HTTP 304 *unchanged is set and the buffer is left alone.
inline const char* fetchFrame(const char* url, Framebuffer& out, String& etag, bool* unchanged) {
  static char err[48];
  *unchanged = false;
  if (WiFi.status() != WL_CONNECTED) return "wi-fi offline";
  bool https = isHttpsUrl(url);
  if (!https && strncmp(url, "http://", 7)) return "URL must start with http:// or https://";

  SecureClient sc; NetworkClient plain;
  if (https) secureClient(sc);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(10000); http.setTimeout(15000); http.useHTTP10(true);
  bool ok = https ? http.begin(sc, url) : http.begin(plain, url);
  if (!ok) return "bad content URL";
  const char* keys[] = {"ETag"};
  http.collectHeaders(keys, 1);
  if (etag.length()) http.addHeader("If-None-Match", etag);
  int code = http.GET();
  if (code == 304) { http.end(); *unchanged = true; return nullptr; }
  if (code != 200) { snprintf(err, sizeof err, "content HTTP %d", code); http.end(); return err; }
  int len = http.getSize();
  if (len >= 0 && len != (int)FB_BYTES) { snprintf(err, sizeof err, "content is %d bytes, need %u", len, (unsigned)FB_BYTES); http.end(); return err; }

  NetworkClient* s = http.getStreamPtr();
  size_t got = 0; unsigned long last = millis();
  while (got < FB_BYTES && millis() - last < 15000) {
    int n = s->read(out.px + got, FB_BYTES - got);
    if (n > 0) { got += n; last = millis(); }
    else if (!http.connected() && !s->available()) break;
    else delay(2);
  }
  String newTag = http.header("ETag");
  http.end();
  if (got != FB_BYTES) { snprintf(err, sizeof err, "content cut off at %u bytes", (unsigned)got); return err; }
  etag = newTag;
  return nullptr;
}

}  // namespace septa
