/* Fort Washington SEPTA departure board
 * Board:   Seeed XIAO ESP32-C6  (Arduino core "esp32" by Espressif, 3.x)
 * Display: Qiyun QYEG1085BNF86BF0 10.85" 1360x480 B/W on DESPI-C02 adapter
 * Library: ArduinoJson 7.x (Library Manager). Everything else is in the core.
 *
 * Behaviour
 *  - First boot (or hold D5 to GND at power-on): opens Wi-Fi "SEPTA-Board-Setup". Join it, enter home Wi-Fi.
 *  - Polls SEPTA NextToArrive (default once a minute, slower at night); redraws only when something changes.
 *  - Changes go out as a partial refresh (no flash). Every N partials or every hour: a full refresh.
 *  - Feed/Wi-Fi problems are shown on the board, never silently.
 *  - Web UI at http://<board-ip>/: status, live panel preview, settings (saved in flash), developer tools.
 *  - Power: Wi-Fi modem sleep on by default; optional 80 MHz CPU; night-mode polling. No deep sleep (the
 *    web UI and the minute-accurate countdown need the board awake).
 */
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "logbuf.h"
#include "settings.h"
#include "framebuffer.h"
#include "rail_logic.h"
#include "layout_v2.h"
#include "epd_qyeg1085.h"
#include "septa_api.h"
#include "wifi_portal.h"
#include "web_ui.h"

using namespace septa;

static const char* FW_VERSION = "1.1.0 " __DATE__;

namespace septa { LogRing LOGR; }

static EpdQyeg1085 epd;
static WifiPortal portal;
static WebUi ui;
static Settings cfg;
static Framebuffer shown;       // what the panel currently displays
static Framebuffer next;        // what we want it to display
static BoardData board;
static Train fetched[SEPTA_RESULTS];
static int fetchedCount = 0;
static Train simTrains[SEPTA_RESULTS];
static int simCount = 0;
static bool simActive = false;

static unsigned long lastPollMs = 0, lastGoodMs = 0, lastFullMs = 0;
static unsigned long pollIntervalMs = POLL_INTERVAL_MS;
static bool nightNow = false;
static int partialsSinceFull = 0;
static bool everPainted = false;
static char feedStatus[48] = "booting";
static unsigned long lastEpdMs = 0; static bool lastEpdFull = false;

// ---- time helpers ----
static bool localTime(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;        // NTP not synced yet
  localtime_r(&now, &t); return true;
}
static void clockString(char* out, size_t n) {
  struct tm t;
  if (!localTime(t)) { snprintf(out, n, "--:--"); return; }
  strftime(out, n, "%I:%M %p", &t);
  if (out[0] == '0') memmove(out, out + 1, strlen(out));   // "09:05 AM" -> "9:05 AM"
}

// ---- display policy ----
static void commit(bool full, Rect left, Rect right) {
  unsigned long t0 = millis();
  if (full) { epd.fullUpdate(next); partialsSinceFull = 0; lastFullMs = millis(); }
  else      { epd.partialUpdate(shown, next, left, right); ++partialsSinceFull; }
  memcpy(shown.px, next.px, FB_BYTES);
  everPainted = true; lastEpdMs = millis() - t0; lastEpdFull = full;
  LOG("[epd] %s update in %lu ms (refresh wait %lu ms)%s\n", full ? "FULL" : "partial",
      lastEpdMs, (unsigned long)epd.lastRefreshMs(), epd.lastBusyTimedOut() ? "  ** BUSY TIMEOUT **" : "");
}

static void paint(bool forceFull) {
  renderV2(next, board);
  Rect left = next.diffRect(shown, 0, HALF_W), right = next.diffRect(shown, HALF_W, W);
  if (!forceFull && everPainted && left.empty() && right.empty()) return;   // nothing changed: do nothing
  bool full = forceFull || !everPainted || partialsSinceFull >= cfg.fullEveryN
              || (millis() - lastFullMs) > cfg.fullMaxAgeMin * 60000UL
              || next.clearedPixels(shown) > FULL_REFRESH_IF_CLEARED_PX;      // big black->white change: avoid ghosting
  commit(full, left, right);
}

// Developer aid: border, half labels, stripes. Confirms half order, polarity and both controllers alive.
static void paintTestPattern() {
  next.clear(false);
  next.fillRect(0, 0, W, 6, true); next.fillRect(0, H - 6, W, 6, true);
  next.fillRect(0, 0, 6, H, true); next.fillRect(W - 6, 0, 6, H, true);
  next.fillRect(HALF_W - 2, 0, 4, H, true);
  for (int x = 40; x < HALF_W - 40; x += 32) next.fillRect(x, 60, 16, 60, true);       // left: vertical bars
  for (int y = 60; y < 120; y += 16) next.fillRect(HALF_W + 40, y, HALF_W - 80, 8, true); // right: horizontal bars
  next.text(60, 300, "LEFT  (CS)", 3, true);
  next.text(HALF_W + 60, 300, "RIGHT  (CS2)", 3, true);
  next.text(60, 420, "TOP-LEFT CORNER IS HERE", 1, true);
  next.fillRect(HALF_W + 60, 360, 200, 90, true); next.text(HALF_W + 80, 425, "INK", 3, false);
  commit(true, {0, 0, 0, 0}, {0, 0, 0, 0});
}

static void showMessage(const char* fault, const char* detail) {
  board.fault = fault; board.faultDetail = detail; board.n = 0;
  clockString(board.clock, sizeof board.clock);
  paint(false);
}

// ---- main cycle ----
static void poll() {
  const char* err = nullptr;
  int n;
  if (simActive) { n = simCount; memcpy(fetched, simTrains, sizeof(Train) * simCount); }
  else n = fetchDepartures(fetched, SEPTA_RESULTS, err, cfg.origin, cfg.dest);
  if (n >= 0) { fetchedCount = n; lastGoodMs = millis(); clockString(board.updated, sizeof board.updated); }

  struct tm t;
  bool haveTime = localTime(t);
  board.nowMin = haveTime ? t.tm_hour * 60 + t.tm_min : 0;
  clockString(board.clock, sizeof board.clock);
  nightNow = haveTime && cfg.isNight(t.tm_hour);
  pollIntervalMs = (nightNow ? cfg.nightPollSec : cfg.pollSec) * 1000UL;

  bool stale = !lastGoodMs || (millis() - lastGoodMs) > cfg.staleMin * 60000UL;
  if (!haveTime)                { board.fault = "WAITING FOR NETWORK TIME"; board.faultDetail = nullptr; board.n = 0; }
  else if (n < 0 && stale)      { board.fault = "FEED UNAVAILABLE";         board.faultDetail = err;      board.n = 0; }
  else                          { board.fault = nullptr; board.faultDetail = nullptr;
                                  board.n = pickUpcoming(fetched, fetchedCount, board.nowMin, board.trains, 3); }
  snprintf(feedStatus, sizeof feedStatus, "%s", err ? err : "ok");
  LOG("[poll] %s, %d trains, showing %d%s%s\n", feedStatus, fetchedCount, board.n, simActive ? " (simulated)" : "", nightNow ? " (night)" : "");
  paint(false);
}

// ---- settings that apply live ----
static void applySettings() {
  WiFi.setSleep(cfg.wifiSleep);
  setCpuFrequencyMhz(cfg.cpu80 ? 80 : 160);
  pollIntervalMs = (nightNow ? cfg.nightPollSec : cfg.pollSec) * 1000UL;   // take effect now, not after the next poll
}

// ---- web hooks ----
static void trainJson(JsonObject o, const Train& t, int nowMin) {
  char hh[8], ap[4], st[24];
  o["number"] = t.number;
  fmtClock(t.schedMin, hh, sizeof hh, ap, sizeof ap); o["sched"] = String(hh) + " " + ap;
  fmtClock(t.schedMin + t.lateMin, hh, sizeof hh, ap, sizeof ap); o["est"] = String(hh) + " " + ap;
  statusLabel(t.delayRaw, st, sizeof st); o["status"] = st;
  o["delayRaw"] = t.delayRaw; o["lateMin"] = t.lateMin; o["direct"] = t.direct;
  o["inMin"] = minutesUntil(t, nowMin);
}

static void fillStatus(JsonObject o) {
  struct tm t; char ts[32] = "time not synced";
  if (localTime(t)) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &t);
  o["time"] = ts; o["ssid"] = WiFi.SSID(); o["rssi"] = WiFi.RSSI(); o["ip"] = WiFi.localIP().toString();
  o["feed"] = feedStatus; o["sim"] = simActive; o["fetched"] = fetchedCount; o["shown"] = board.n;
  if (lastGoodMs) o["lastGoodS"] = (millis() - lastGoodMs) / 1000; else o["lastGoodS"] = nullptr;
  unsigned long since = millis() - lastPollMs;
  o["nextPollS"] = since >= pollIntervalMs ? 0 : (pollIntervalMs - since) / 1000;
  o["night"] = nightNow; o["partials"] = partialsSinceFull;
  o["uptime"] = millis() / 1000; o["heap"] = ESP.getFreeHeap(); o["cpuMhz"] = getCpuFrequencyMhz(); o["fw"] = FW_VERSION;
  JsonObject e = o["epd"].to<JsonObject>();
  e["kind"] = everPainted ? (lastEpdFull ? "full" : "partial") : "none"; e["ms"] = lastEpdMs;
  e["refreshMs"] = epd.lastRefreshMs(); e["busyTimeout"] = epd.lastBusyTimedOut();
  JsonArray a = o["trains"].to<JsonArray>();
  for (int i = 0; i < board.n; ++i) trainJson(a.add<JsonObject>(), board.trains[i], board.nowMin);
}

static void fillFeed(JsonArray a) {
  for (int i = 0; i < fetchedCount; ++i) trainJson(a.add<JsonObject>(), fetched[i], board.nowMin);
}

static bool doAction(const char* a) {
  LOG("[ui] action %s\n", a);
  if (!strcmp(a, "poll"))      { poll(); lastPollMs = millis(); return true; }
  if (!strcmp(a, "full"))      { paint(true); return true; }
  if (!strcmp(a, "pattern"))   { paintTestPattern(); return true; }
  if (!strcmp(a, "white"))     { next.clear(false); commit(true, {0, 0, 0, 0}, {0, 0, 0, 0}); everPainted = false; return true; }
  if (!strcmp(a, "reboot"))    { ESP.restart(); return true; }
  if (!strcmp(a, "resetwifi")) { portal.clearCredentials(); ESP.restart(); return true; }
  return false;
}

static bool doSimulate(JsonArrayConst rows) {
  int n = 0;
  for (JsonObjectConst r : rows) {
    if (n >= SEPTA_RESULTS) break;
    Train& t = simTrains[n];
    snprintf(t.number, sizeof t.number, "%s", r["number"] | "0000");
    snprintf(t.departure, sizeof t.departure, "%s", r["departure"] | "");
    snprintf(t.delayRaw, sizeof t.delayRaw, "%s", r["delay"] | "On time");
    t.schedMin = parseClock(t.departure); t.lateMin = parseDelayMinutes(t.delayRaw); t.direct = true;
    if (t.schedMin >= 0) ++n;
  }
  simCount = n; simActive = n > 0;
  LOG("[sim] %s (%d trains)\n", simActive ? "on" : "off", n);
  poll(); lastPollMs = millis();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  LOG("\nFort Washington SEPTA board %s\n", FW_VERSION);
  cfg.load();

  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  epd.begin({PIN_EPD_CS, PIN_EPD_CS2, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY});

  String ssid, pass;
  bool forceSetup = digitalRead(SETUP_BUTTON_PIN) == LOW;
  if (forceSetup || !portal.loadCredentials(ssid, pass)) {
    showMessage("WI-FI SETUP", "JOIN " SETUP_AP_NAME " ON YOUR PHONE");
    portal.runSetupBlocking();                 // reboots when saved
  }

  showMessage("CONNECTING TO WI-FI", ssid.c_str());
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    LOG("[wifi] failed; opening setup portal\n");
    showMessage("WI-FI FAILED", "JOIN " SETUP_AP_NAME " TO RE-ENTER PASSWORD");
    portal.runSetupBlocking();
  }
  LOG("[wifi] %s  %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  WebHooks hooks;
  hooks.status = fillStatus; hooks.feed = fillFeed; hooks.action = doAction;
  hooks.settingsChanged = applySettings; hooks.simulate = doSimulate; hooks.shown = &shown;
  ui.begin(portal.server(), &cfg, hooks);
  LOG("[ui] http://%s/\n", WiFi.localIP().toString().c_str());

  t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 15000) delay(100);
  applySettings();
  LOG("[power] modem sleep %s, cpu %u MHz\n", cfg.wifiSleep ? "on" : "off", getCpuFrequencyMhz());
  poll();
  lastPollMs = millis();
}

void loop() {
  portal.handle();
  if (millis() - lastPollMs >= pollIntervalMs) {
    lastPollMs = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    poll();
  }
  delay(20);
}
