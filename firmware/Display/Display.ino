/* E-paper display base firmware, with remote updates.
 * Board:   Seeed XIAO ESP32-C6 (esp32 core 3.1.3), custom partitions.csv with two OTA app slots
 * Display: Qiyun QYEG1085BNF86BF0 10.85" 1360x480 B/W on a DESPI-C02 V3 adapter
 *
 * What it shows (in priority order):
 *   1. A picture pushed from the web page or POST /api/fb (stays until "Back to clock" or a restart)
 *   2. A frame fetched from the content URL, if one is set (Settings)
 *   3. Otherwise the home screen (dashboard.h): greeting, clock, weather now, a 24-hour forecast panorama with
 *      night shaded from the real sun position, moon phase and daylight trend; firmware / network / update
 *      status in the footer. The forecast is from Open-Meteo (no API key), fetched once an hour.
 *      Or (Settings > Screen) the SEPTA subway map for the display hung vertically (subway.h), with a clock.
 *
 * How it updates itself (see ota.h): checks a manifest on GitHub every N minutes and installs newer builds;
 * or upload a .bin at http://<board-ip>/ (Update tab) or with curl. New firmware that can't stay up on Wi-Fi
 * rolls back to the previous one automatically.
 *
 * Build and flash: tools/fw.py (see firmware/README.md). Never put passwords or keys in this code.
 */
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <esp_system.h>
#include "config.h"
#include "logbuf.h"
#include "settings.h"
#include "framebuffer.h"
#include "screen.h"
#include "dashboard.h"
#include "subway.h"
#include "weather.h"
#include "epd_qyeg1085.h"
#include "ota.h"
#include "content.h"
#include "wifi_portal.h"
#include "web_ui.h"

using namespace septa;

namespace septa { LogRing LOGR; }

static EpdQyeg1085 epd;
static WifiPortal portal;
static WebUi ui;
static Settings cfg;
static Ota ota;
static Framebuffer shown;   // what the glass shows now
static Framebuffer next;    // what we want it to show
// Two 81,600-byte buffers. Do NOT call epd.clearWhite(): it allocates a third one.

enum class Mode { Clock, Content, Pushed };
static Mode mode = Mode::Clock;
static bool everPainted = false, lastEpdFull = false;
static int partialsSinceFull = 0;
static unsigned long lastFullMs = 0, lastEpdMs = 0;
static unsigned long lastCheckMs = 0, lastContentMs = 0, rebootAtMs = 0;
static bool contentOk = false;
static char contentStatus[64] = "off";
static String contentEtag;
static char lastMinute[8] = "";
static Weather wx;                       // last good forecast
static char wxStatus[48] = "not fetched yet";
static int wxHourKey = -1;               // hour (day*24+hour) of the last good fetch
static unsigned long wxTryMs = 0;        // last attempt, for retry spacing
static bool wxTried = false;
// ---------- display policy ----------
static void commit(bool full) {
  Rect l = next.diffRect(shown, 0, HALF_W), r = next.diffRect(shown, HALF_W, W);
  if (!full && everPainted && l.empty() && r.empty()) return;          // nothing changed: leave the glass alone
  full = full || !everPainted || partialsSinceFull >= cfg.fullEveryN
         || millis() - lastFullMs > cfg.fullMaxAgeMin * 60000UL
         || next.clearedPixels(shown) > FULL_REFRESH_IF_CLEARED_PX;    // big black->white change ghosts under partial
  unsigned long t0 = millis();
  if (full) { epd.fullUpdate(next); partialsSinceFull = 0; lastFullMs = millis(); }
  else      { epd.partialUpdate(shown, next, l, r); ++partialsSinceFull; }
  memcpy(shown.px, next.px, FB_BYTES);
  everPainted = true; lastEpdFull = full; lastEpdMs = millis() - t0;
  LOG("[epd] %s in %lu ms%s\n", full ? "FULL" : "partial", lastEpdMs, epd.lastBusyTimedOut() ? "  ** BUSY TIMEOUT **" : "");
}

static void showMessage(const char* title, const char* detail, const char* detail2 = nullptr) {
  renderMessage(next, title, detail, detail2); commit(false);
}

static void showBanner(const char* text) {
  memcpy(next.px, shown.px, FB_BYTES);
  drawBanner(next, text);
  commit(false);
}

// ---------- the clock screen ----------
static bool localTime(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;   // NTP not synced yet
  localtime_r(&now, &t); return true;
}

static const char* updateShort() {
  if (!cfg.updateUrl[0]) return "UPDATES OFF";
  if (FW_BUILD == 0) return "DEV BUILD";
  const char* r = ota.result();
  if (!strcmp(r, "up to date")) return "UP TO DATE";
  if (!strcmp(r, "update available")) return "UPDATE AVAILABLE";
  if (!strcmp(r, "not checked yet")) return "NOT CHECKED YET";
  return "UPDATE CHECK FAILED";
}

// Forecast: fetched once an hour (at the first repaint of the hour), so the panorama's big hourly change and
// the new data land together in one full refresh. After a failure, retry every 10 minutes.
static void maybeFetchWeather() {
  if (cfg.screen != 0) return;   // only the home screen shows weather
  if (!cfg.weather) { wx.ok = false; snprintf(wxStatus, sizeof wxStatus, "off"); return; }
  struct tm t;
  if (!localTime(t) || WiFi.status() != WL_CONNECTED) return;
  int key = t.tm_yday * 24 + t.tm_hour;
  if (key == wxHourKey) return;
  if (wxTried && millis() - wxTryMs < 10UL * 60000UL && strcmp(wxStatus, "ok")) return;   // failed recently
  wxTried = true; wxTryMs = millis();
  unsigned long t0 = millis();
  const char* err = fetchWeather(cfg.lat, cfg.lon, cfg.metric, wx);
  if (err) { snprintf(wxStatus, sizeof wxStatus, "%s", err); LOG("[wx] %s\n", err); return; }
  wxHourKey = key; snprintf(wxStatus, sizeof wxStatus, "ok");
  LOG("[wx] %.1f%c code %d, %d hours, %lu ms, heap %u\n", wx.temp, cfg.metric ? 'C' : 'F', wx.code, wx.n,
      millis() - t0, (unsigned)ESP.getFreeHeap());
}

static void renderClock(Framebuffer& fb) {
  char left[64], right[96], notice[80] = "", status[64] = "";
  if (cfg.screen == 1) {   // subway map, portrait
    snprintf(left, sizeof left, "BUILD %d", FW_BUILD);
    snprintf(right, sizeof right, "%s  %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "OFFLINE", updateShort());
    SubwayInfo si; si.now = time(nullptr); si.turnLeft = cfg.turnLeft; si.footerLeft = left; si.footerRight = right;
    renderSubway(fb, si);
    return;
  }
  snprintf(left, sizeof left, "FIRMWARE %s  BUILD %d", FW_VERSION, FW_BUILD);
  snprintf(right, sizeof right, "%s   EPAPER.LOCAL   %s",
           WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "OFFLINE", updateShort());
  if (cfg.contentUrl[0] && !contentOk) snprintf(notice, sizeof notice, "CONTENT: %s", contentStatus);
  DashInfo d;
  d.now = time(nullptr); d.lat = cfg.lat; d.lon = cfg.lon; d.name = cfg.name;
  // A forecast older than 6 hours is not worth showing.
  bool fresh = wx.ok && cfg.weather && d.now - wx.fetched < 6 * 3600;
  d.wx = fresh ? &wx : nullptr;
  if (!cfg.weather) snprintf(status, sizeof status, "WEATHER IS OFF IN SETTINGS");
  else if (wx.ok && !fresh) snprintf(status, sizeof status, "LAST FORECAST IS OVER 6 HOURS OLD");
  else snprintf(status, sizeof status, "%s", wxStatus);
  d.wxStatus = status;
  d.footerLeft = left; d.footerRight = right; d.notice = notice;
  renderDashboard(fb, d);
}

static void paintClock() { maybeFetchWeather(); renderClock(next); commit(false); }

// Fetch the content URL; on failure fall back to the clock with a notice.
static void refreshContent() {
  lastContentMs = millis();
  bool unchanged = false;
  const char* err = fetchFrame(cfg.contentUrl, next, contentEtag, &unchanged);
  if (!err) {
    contentOk = true; snprintf(contentStatus, sizeof contentStatus, "ok");
    if (unchanged) memcpy(next.px, shown.px, FB_BYTES); else commit(false);
    return;
  }
  contentOk = false; contentEtag = "";
  snprintf(contentStatus, sizeof contentStatus, "%s", err);
  LOG("[content] %s\n", err);
  paintClock();
}

// Re-evaluate what should be on screen right now.
static void resumeAuto() {
  mode = cfg.contentUrl[0] ? Mode::Content : Mode::Clock;
  if (mode == Mode::Content) { contentEtag = ""; refreshContent(); }
  else { contentOk = false; snprintf(contentStatus, sizeof contentStatus, "off"); paintClock(); }
}

// Developer aid: border, half labels, stripes. Confirms half order, polarity and both controllers.
static void paintTestPattern() {
  next.clear(false);
  next.fillRect(0, 0, W, 6, true); next.fillRect(0, H - 6, W, 6, true);
  next.fillRect(0, 0, 6, H, true); next.fillRect(W - 6, 0, 6, H, true);
  next.fillRect(HALF_W - 2, 0, 4, H, true);
  for (int x = 40; x < HALF_W - 40; x += 32) next.fillRect(x, 60, 16, 60, true);
  for (int y = 60; y < 120; y += 16) next.fillRect(HALF_W + 40, y, HALF_W - 80, 8, true);
  next.text(60, 300, "LEFT  (CS)", 3, true);
  next.text(HALF_W + 60, 300, "RIGHT  (CS2)", 3, true);
  next.fillRect(HALF_W + 60, 360, 200, 90, true); next.text(HALF_W + 80, 425, "INK", 3, false);
  commit(true);
}

// ---------- settings / web hooks ----------
static void applySettings() {
  WiFi.setSleep(cfg.wifiSleep);
  configTzTime(cfg.tz, "pool.ntp.org", "time.nist.gov");
  wxHourKey = -1; wxTried = false; wx.ok = false;   // location or units may have changed: refetch
  if (mode != Mode::Pushed) resumeAuto();
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";   case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";        case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";  case ESP_RST_USB: return "usb";  default: return "other";
  }
}

static void fillStatus(JsonObject o) {
  struct tm t; char ts[32] = "time not synced";
  if (localTime(t)) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &t);
  o["time"] = ts; o["ssid"] = WiFi.SSID(); o["rssi"] = WiFi.RSSI(); o["ip"] = WiFi.localIP().toString();
  o["mode"] = mode == Mode::Pushed ? "pushed picture" : mode == Mode::Content ? (contentOk ? "content URL" : "clock (content failed)") : "clock";
  o["content"] = contentStatus; o["weather"] = wxStatus; o["partials"] = partialsSinceFull;
  JsonObject e = o["epd"].to<JsonObject>();
  e["kind"] = everPainted ? (lastEpdFull ? "full" : "partial") : "none"; e["ms"] = lastEpdMs; e["busyTimeout"] = epd.lastBusyTimedOut();
  o["uptime"] = millis() / 1000; o["heap"] = ESP.getFreeHeap(); o["minHeap"] = ESP.getMinFreeHeap(); o["reset"] = resetReason();
}

static String doAction(const char* a) {
  LOG("[ui] action %s\n", a);
  if (!strcmp(a, "check"))   { lastCheckMs = millis(); ota.check(cfg.updateUrl, false, false); return String("Check: ") + ota.result(); }
  if (!strcmp(a, "install")) {
    lastCheckMs = millis();
    if (ota.check(cfg.updateUrl, true, true)) { rebootAtMs = millis() + 1500; return "Installed. Restarting."; }
    return String("Not installed: ") + ota.result();
  }
  if (!strcmp(a, "full"))    { if (mode == Mode::Clock) renderClock(next); else memcpy(next.px, shown.px, FB_BYTES); commit(true); return "Full refresh done"; }
  if (!strcmp(a, "pattern")) { mode = Mode::Pushed; paintTestPattern(); return "Test pattern shown"; }
  if (!strcmp(a, "white"))   { mode = Mode::Pushed; next.clear(false); commit(true); return "Cleared"; }
  if (!strcmp(a, "clock"))   { resumeAuto(); return "Back to automatic"; }
  if (!strcmp(a, "reboot"))  { ESP.restart(); return "Restarting"; }
  if (!strcmp(a, "resetwifi")) { portal.clearCredentials(); ESP.restart(); return "Restarting"; }
  return "";
}

// ---------- setup / loop ----------
static void connectWifi() {
  String ssid, pass;
  bool forceSetup = digitalRead(SETUP_BUTTON_PIN) == LOW;
  if (forceSetup || !portal.loadCredentials(ssid, pass)) {
    showMessage("WI-FI SETUP", "JOIN THE WI-FI NETWORK " SETUP_AP_NAME " ON YOUR PHONE");
    portal.runSetupBlocking();                       // reboots when saved
  }
  // Log why the station drops (802.11 reason codes: 15 = wrong password, 201 = network not found, ...).
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    static int lastReason = -1; int r = info.wifi_sta_disconnected.reason;
    if (r != lastReason) { LOG("[wifi] disconnected, reason %d\n", r); lastReason = r; }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.setHostname("epaper");                       // before mode(): the name is applied when the interface starts
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.setSleep(false);
  LOG("[wifi] connecting to '%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) delay(100);
  if (WiFi.status() == WL_CONNECTED) { LOG("[wifi] %s  %s\n", ssid.c_str(), WiFi.localIP().toString().c_str()); return; }
  LOG("[wifi] not connected after %lu s (status %d); scanning\n", WIFI_CONNECT_TIMEOUT_MS / 1000, (int)WiFi.status());
  WiFi.setAutoReconnect(false); WiFi.disconnect(); delay(200);   // a scan fails (-2) while it's still retrying
  int n = WiFi.scanNetworks();
  if (n < 0) { delay(1000); n = WiFi.scanNetworks(); }
  WiFi.setAutoReconnect(true);
  bool seen = false;
  for (int i = 0; i < n; ++i) if (WiFi.SSID(i) == ssid) {
    seen = true; LOG("[wifi] '%s' visible: %d dBm, channel %d, auth %d\n", ssid.c_str(), WiFi.RSSI(i), WiFi.channel(i), (int)WiFi.encryptionType(i));
  }
  if (!seen) LOG("[wifi] '%s' not among %d visible 2.4 GHz networks\n", ssid.c_str(), n);
  if (ota.pending()) { LOG("[wifi] new firmware: will roll back if this persists\n"); WiFi.begin(ssid.c_str(), pass.c_str()); return; }
  LOG("[wifi] failed; opening setup portal for 10 minutes\n");
  showMessage(seen ? "WI-FI PASSWORD REJECTED?" : "WI-FI NETWORK NOT FOUND", ssid.c_str(),
              "JOIN " SETUP_AP_NAME " ON YOUR PHONE TO CHANGE IT, OR WAIT: RETRYING IN 10 MIN");
  portal.runSetupBlocking(10UL * 60UL * 1000UL);
}

void setup() {
  Serial.begin(115200);
  for (unsigned long t = millis(); !Serial && millis() - t < 2000;) delay(10);   // let a USB monitor attach
  delay(300);
  LOG("\nE-paper display %s build %d\n", FW_VERSION, FW_BUILD);
  cfg.load();
  ota.begin();
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  epd.begin({PIN_EPD_CS, PIN_EPD_CS2, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY});
  shown.clear(false);

  ota.onInstalling = [](const char* v) {
    char b[64];
    if (!strcmp(v, "UPLOAD")) snprintf(b, sizeof b, "INSTALLING UPLOADED FIRMWARE...");
    else snprintf(b, sizeof b, "INSTALLING UPDATE %s...", v);
    showBanner(b);
  };

  connectWifi();
  configTzTime(cfg.tz, "pool.ntp.org", "time.nist.gov");
  if (MDNS.begin("epaper")) MDNS.addService("http", "tcp", 80);

  WebHooks hooks;
  hooks.status = fillStatus; hooks.action = doAction; hooks.settingsChanged = applySettings;
  hooks.frameBegin = []() -> uint8_t* { return next.px; };
  hooks.frameEnd = [](bool ok) {
    if (ok) { mode = Mode::Pushed; commit(false); LOG("[ui] picture pushed\n"); }
    else memcpy(next.px, shown.px, FB_BYTES);   // drop the partial frame
  };
  hooks.shown = &shown; hooks.ota = &ota;
  ui.begin(portal.server(), &cfg, hooks);
  LOG("[ui] http://%s/  (http://epaper.local/)\n", WiFi.localIP().toString().c_str());

  unsigned long t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 15000) delay(100);
  WiFi.setSleep(cfg.wifiSleep);
  resumeAuto();
  // First update check ~30 s after boot, then every checkMin minutes.
  lastCheckMs = millis() - cfg.checkMin * 60000UL + 30000UL;
}

void loop() {
  portal.handle();
  bool up = WiFi.status() == WL_CONNECTED;
  ota.health(up);

  if (rebootAtMs && (long)(millis() - rebootAtMs) > 0) ESP.restart();

  if (up && cfg.updateUrl[0] && millis() - lastCheckMs > cfg.checkMin * 60000UL) {
    lastCheckMs = millis();
    if (ota.check(cfg.updateUrl, cfg.autoUpdate && !ota.pending(), false)) { delay(1000); ESP.restart(); }
    if (mode == Mode::Clock || (mode == Mode::Content && !contentOk)) paintClock();   // footer status may change
  }

  if (mode == Mode::Content && millis() - lastContentMs > cfg.contentSec * 1000UL) refreshContent();

  // Clock: repaint when the minute changes (partial refresh, ~0.6 s).
  if (mode == Mode::Clock || (mode == Mode::Content && !contentOk)) {
    struct tm t; char m[8] = "none";
    if (localTime(t)) snprintf(m, sizeof m, "%02d%02d", t.tm_hour, t.tm_min);
    if (strcmp(m, lastMinute)) { strcpy(lastMinute, m); paintClock(); }
  }
  delay(20);
}
