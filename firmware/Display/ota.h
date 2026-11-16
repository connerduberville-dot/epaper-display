// Firmware updates, two ways in:
//   1. Pull: the board fetches a small manifest.json over HTTPS (GitHub release "ota"), and if it names a newer
//      build, downloads firmware.bin, checks its size and SHA-256 while streaming it into the spare app slot,
//      and reboots into it. TLS is verified against the Mozilla root bundle built into the ESP32 core.
//   2. Push on the LAN: POST /update (multipart upload) from a browser or curl; see web_ui.h.
//
// Safety net (both ways): a new image boots "pending". It must reach Wi-Fi and stay up for
// OTA_CONFIRM_AFTER_MS before we mark it good. If it crashes or reboots first, the bootloader goes back to
// the previous firmware by itself; if it can't get on Wi-Fi within OTA_ROLLBACK_NO_WIFI_MS, we roll back.
//
// manifest.json: {"build": 12, "version": "1.0.0+12", "url": "https://.../firmware.bin",
//                 "size": 1234567, "sha256": "<64 hex>"}
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <functional>
#include "config.h"
#include "logbuf.h"

#if __has_include(<NetworkClientSecure.h>)
  #include <NetworkClientSecure.h>
  using SecureClient = NetworkClientSecure;
#else
  #include <WiFiClientSecure.h>
  using SecureClient = WiFiClientSecure;
#endif

// The Mozilla CA bundle compiled into the core's mbedtls (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

// Tell the core not to auto-confirm a freshly installed image; we confirm it ourselves once it proves itself.
// The core declares it in C (esp32-hal-misc.c), so it must have C linkage or the weak default wins silently.
extern "C" bool verifyRollbackLater() { return true; }

namespace septa {

// Verified HTTPS client. Every outbound HTTPS request in this firmware goes through this.
inline void secureClient(SecureClient& c) {
  c.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
  c.setHandshakeTimeout(15);
}

inline bool isHttpsUrl(const char* u) { return u && !strncmp(u, "https://", 8); }

// GET with redirects followed by hand, opening a fresh connection for every hop. HTTPClient's built-in
// following fails with -1 on GitHub's release download (github.com -> release-assets.githubusercontent.com):
// NetworkClientSecure::stop() forgets the CA bundle, so the second TLS handshake has no roots
// ("SSL - No CA Chain is set"). Found on this board, Nov 16 2026.
// An https URL is never redirected to plain http. `prep` can add request headers before each GET.
// Returns the final HTTP status (or a negative HTTPClient error); on success `http` is ready to read.
inline int httpGetFollow(HTTPClient& http, SecureClient& sc, NetworkClient& plain, String url,
                         std::function<void(HTTPClient&)> prep = nullptr) {
  bool wasHttps = url.startsWith("https://");
  for (int hop = 0; hop < 6; ++hop) {
    bool https = url.startsWith("https://");
    if (!https && (wasHttps || !url.startsWith("http://"))) return HTTPC_ERROR_CONNECTION_REFUSED;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(false);
    if (!(https ? http.begin(sc, url) : http.begin(plain, url))) return HTTPC_ERROR_CONNECTION_REFUSED;
    if (prep) prep(http);
    int code = http.GET();
    if (code < 0) {
      char e[96] = ""; if (https) sc.lastError(e, sizeof e);
      int h0 = url.indexOf("//") + 2, h1 = url.indexOf('/', h0);
      LOG("[http] %s: error %d (hop %d, url %u chars) %s\n", url.substring(h0, h1 < 0 ? url.length() : h1).c_str(),
          code, hop, url.length(), e);
    }
    if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308) return code;
    String loc = http.getLocation();
    http.end(); sc.stop(); plain.stop();
    secureClient(sc);   // stop() drops the CA bundle ("No CA Chain is set"): attach it again for the next hop
    if (!loc.length()) return code;
    if (loc.startsWith("/")) { int p = url.indexOf('/', url.indexOf("//") + 2); loc = (p < 0 ? url : url.substring(0, p)) + loc; }
    url = loc;
  }
  return HTTPC_ERROR_TOO_LESS_RAM;   // too many redirects
}

class Ota {
 public:
  // Called just before an install starts (e.g. to show "UPDATING" on the panel). Argument: the new version.
  std::function<void(const char*)> onInstalling;

  // ---- boot / health ----
  void begin() {
    const esp_partition_t* run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    pending_ = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    snprintf(slot_, sizeof slot_, "%s", run ? run->label : "?");
    LOG("[ota] running from %s%s\n", slot_, pending_ ? " (NEW: waiting to confirm)" : "");
  }
  // Call every loop. Confirms a new image after it has been on Wi-Fi for a while, or rolls it back.
  void health(bool wifiUp) {
    if (!pending_) return;
    if (wifiUp) { if (!wifiSinceMs_) wifiSinceMs_ = millis(); }
    else wifiSinceMs_ = 0;
    if (wifiSinceMs_ && millis() - wifiSinceMs_ > OTA_CONFIRM_AFTER_MS) {
      esp_ota_mark_app_valid_cancel_rollback();
      pending_ = false;
      LOG("[ota] new firmware confirmed good\n");
    } else if (!wifiUp && millis() > OTA_ROLLBACK_NO_WIFI_MS) {
      LOG("[ota] new firmware never reached Wi-Fi: rolling back\n");
      delay(200);
      esp_ota_mark_app_invalid_rollback_and_reboot();   // reboots into the previous slot
    }
  }
  bool pending() const { return pending_; }
  const char* slot() const { return slot_; }

  // ---- pull from a manifest URL ----
  // install=false only checks. force=true installs even if the build number isn't newer (and on dev builds).
  // Returns true if an update was installed (the caller should reboot after a short pause).
  bool check(const char* manifestUrl, bool install, bool force) {
    lastCheckMs_ = millis(); everChecked_ = true;
    if (!manifestUrl || !*manifestUrl) { setResult("no update URL set"); return false; }
    if (!isHttpsUrl(manifestUrl)) { setResult("update URL must be https://"); return false; }
    if (WiFi.status() != WL_CONNECTED) { setResult("wi-fi offline"); return false; }

    Manifest m;
    if (!fetchManifest(manifestUrl, m)) return false;
    latestBuild_ = m.build; snprintf(latestVersion_, sizeof latestVersion_, "%s", m.version);

    if (m.build <= FW_BUILD && !force) { setResult("up to date"); return false; }
    if (FW_BUILD == 0 && !force) { setResult("dev build: auto-install paused, newer on GitHub"); return false; }
    if (!install) { setResult("update available"); return false; }
    return download(m);
  }

  // ---- push over the LAN (multipart upload chunks from the web server) ----
  bool uploadBegin(size_t total) {
    uploadErr_[0] = 0;
    if (!Update.begin(total ? total : UPDATE_SIZE_UNKNOWN, U_FLASH)) { uploadFail(); return false; }
    if (onInstalling) onInstalling("UPLOAD");
    return true;
  }
  bool uploadWrite(const uint8_t* d, size_t n) {
    if (uploadErr_[0]) return false;
    if (Update.write(const_cast<uint8_t*>(d), n) != n) { uploadFail(); return false; }
    return true;
  }
  bool uploadEnd() {
    if (uploadErr_[0]) return false;
    if (!Update.end(true)) { uploadFail(); return false; }
    LOG("[ota] LAN upload installed (%u bytes)\n", (unsigned)Update.progress());
    return true;
  }
  void uploadAbort() { if (Update.isRunning()) Update.abort(); if (!uploadErr_[0]) snprintf(uploadErr_, sizeof uploadErr_, "aborted"); }
  const char* uploadError() const { return uploadErr_; }

  // ---- status for the web UI ----
  void toJson(JsonObject o) const {
    o["build"] = FW_BUILD; o["version"] = FW_VERSION; o["slot"] = slot_; o["pending"] = pending_;
    o["result"] = result_; o["latestBuild"] = latestBuild_; o["latestVersion"] = latestVersion_;
    if (everChecked_) o["lastCheckS"] = (millis() - lastCheckMs_) / 1000; else o["lastCheckS"] = nullptr;
    o["canRollBack"] = Update.canRollBack();
  }
  const char* result() const { return result_; }
  int latestBuild() const { return latestBuild_; }

 private:
  struct Manifest { int build = -1; char version[32] = ""; char url[512] = ""; size_t size = 0; char sha[65] = ""; };
  bool pending_ = false; unsigned long wifiSinceMs_ = 0;
  char slot_[12] = "?";
  char result_[64] = "not checked yet";
  int latestBuild_ = -1; char latestVersion_[32] = "";
  unsigned long lastCheckMs_ = 0; bool everChecked_ = false;
  char uploadErr_[64] = "";

  void setResult(const char* r) { snprintf(result_, sizeof result_, "%s", r); LOG("[ota] %s\n", r); }
  void uploadFail() { snprintf(uploadErr_, sizeof uploadErr_, "%s", Update.errorString()); Update.abort(); LOG("[ota] upload failed: %s\n", uploadErr_); }

  bool fetchManifest(const char* url, Manifest& m) {
    SecureClient client; secureClient(client); NetworkClient plain;
    HTTPClient http;
    http.setConnectTimeout(10000); http.setTimeout(15000); http.useHTTP10(true);
    int code = httpGetFollow(http, client, plain, url);
    if (code != 200) {
      char r[64]; snprintf(r, sizeof r, "manifest HTTP %d", code); setResult(r); http.end(); return false;
    }
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream());
    http.end();
    if (e) { setResult("manifest is not valid JSON"); return false; }
    m.build = doc["build"] | -1;
    snprintf(m.version, sizeof m.version, "%s", doc["version"] | "?");
    snprintf(m.url, sizeof m.url, "%s", doc["url"] | "");
    m.size = doc["size"] | 0;
    snprintf(m.sha, sizeof m.sha, "%s", doc["sha256"] | "");
    if (m.build < 0 || !isHttpsUrl(m.url) || m.size < 100000 || strlen(m.sha) != 64) { setResult("manifest incomplete"); return false; }
    return true;
  }

  bool download(const Manifest& m) {
    LOG("[ota] installing build %d (%s), %u bytes\n", m.build, m.version, (unsigned)m.size);
    if (onInstalling) onInstalling(m.version);
    SecureClient client; secureClient(client); NetworkClient plain;
    HTTPClient http;
    http.setConnectTimeout(10000); http.setTimeout(20000); http.useHTTP10(true);
    int code = httpGetFollow(http, client, plain, m.url);
    if (code != 200) { char r[64]; snprintf(r, sizeof r, "firmware HTTP %d", code); setResult(r); http.end(); return false; }
    int len = http.getSize();
    if (len > 0 && (size_t)len != m.size) { setResult("firmware size mismatch"); http.end(); return false; }
    if (!Update.begin(m.size, U_FLASH)) { setResult(Update.errorString()); http.end(); return false; }

    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
    NetworkClient* s = http.getStreamPtr();
    static uint8_t buf[4096];
    size_t got = 0; unsigned long lastData = millis(); bool ok = true;
    while (got < m.size) {
      size_t avail = s->available();
      if (!avail) {
        if (!http.connected() && !s->available()) { ok = false; setResult("connection dropped"); break; }
        if (millis() - lastData > 20000) { ok = false; setResult("download stalled"); break; }
        delay(2); continue;
      }
      size_t want = m.size - got; if (want > sizeof buf) want = sizeof buf; if (want > avail) want = avail;
      int n = s->read(buf, want);
      if (n <= 0) continue;
      mbedtls_sha256_update(&sha, buf, n);
      if (Update.write(buf, n) != (size_t)n) { ok = false; setResult(Update.errorString()); break; }
      got += n; lastData = millis();
    }
    http.end();
    uint8_t digest[32]; mbedtls_sha256_finish(&sha, digest); mbedtls_sha256_free(&sha);
    if (!ok) { Update.abort(); return false; }

    char hex[65]; for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (strcasecmp(hex, m.sha) != 0) { Update.abort(); setResult("SHA-256 mismatch: not installed"); return false; }
    if (!Update.end(true)) { setResult(Update.errorString()); return false; }
    char r[64]; snprintf(r, sizeof r, "installed build %d, rebooting", m.build); setResult(r);
    return true;
  }
};

}  // namespace septa
